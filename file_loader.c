// created by bucka on 2/14/2026.

#include "file_loader.h"
#include "lexer.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// track already-loaded files to prevent circular includes
static char* loaded_files[MAX_INCLUDE_DEPTH];
static int loaded_file_count = 0;

// reset loaded files tracking (call before processing a new compilation)
static void reset_loaded_files() {
    loaded_file_count = 0;
}

// check if a file has already been loaded
static bool is_file_loaded(const char* path) {
    for (int i = 0; i < loaded_file_count; i++) {
        if (strcmp(loaded_files[i], path) == 0) return true;
    }
    return false;
}

// mark a file as loaded
static void mark_file_loaded(const char* path) {
    if (loaded_file_count < MAX_INCLUDE_DEPTH) {
        loaded_files[loaded_file_count++] = strdup(path);
    }
}

char* get_directory(const char* file_path) {
    // find the last / or backslash
    const char* last_sep = nullptr;
    for (const char* p = file_path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }

    if (!last_sep) {
        // no directory separator, use current directory
        return strdup(".");
    }

    size_t len = last_sep - file_path;
    char* dir = malloc(len + 1);
    memcpy(dir, file_path, len);
    dir[len] = '\0';
    return dir;
}

char* resolve_module_path(const char* module_name, const char* source_dir) {
    // convert dots to path separators: "utils.arrays" -> "utils/arrays.lync"
    size_t mod_len = strlen(module_name);
    // allocate: source_dir + / + module_name (with dots->/) + .lync + null
    size_t dir_len = strlen(source_dir);
    char* path = malloc(dir_len + 1 + mod_len + 6); // 6 for ".lync\0"

    // start with source directory
    memcpy(path, source_dir, dir_len);
    path[dir_len] = '/';

    // copy module name, replacing dots with /
    size_t out = dir_len + 1;
    for (size_t i = 0; i < mod_len; i++) {
        path[out++] = (module_name[i] == '.') ? '/' : module_name[i];
    }

    // append .lync
    memcpy(path + out, ".lync", 5);
    path[out + 5] = '\0';

    return path;
}

Program* load_and_parse_file(const char* file_path, int depth) {
    if (depth >= MAX_INCLUDE_DEPTH) {
        stage_error(STAGE_PARSER, NO_LOC,
            "maximum include depth (%d) exceeded — possible circular include for '%s'",
            MAX_INCLUDE_DEPTH, file_path);
        return nullptr;
    }

    if (is_file_loaded(file_path)) {
        // already loaded — not an error, just skip (dont double-include)
        return nullptr;
    }

    // read the file
    FILE* file = fopen(file_path, "r");
    if (!file) {
        return nullptr; // caller will emit the error with location info
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* code = malloc(file_size + 1);
    size_t bytes_read = fread(code, 1, file_size, file);
    code[bytes_read] = '\0';
    fclose(file);

    mark_file_loaded(file_path);

    // lex
    int token_count;
    Token* tokens = tokenize(code, &token_count, file_path);

    // check for lexer errors (theyre collected in the global error collector)
    if (has_errors(g_error_collector)) {
        free(code);
        return nullptr;
    }

    // parse
    Parser parser = {
        .tokens = tokens,
        .count = token_count,
        .size = token_count,
        .pos = 0
    };

    Program* prog = parseProgram(&parser);

    // dont free code — tokens reference it
    // process nested includes in this file too
    if (prog && prog->imports && prog->imports->import_count > 0) {
        char* dir = get_directory(file_path);
        for (int i = 0; i < prog->imports->import_count; i++) {
            IncludeStmt* imp = prog->imports->imports[i];
            // skip std.* imports
            if (strncmp(imp->module_name, "std.", 4) == 0) continue;

            char* nested_path = resolve_module_path(imp->module_name, dir);
            Program* nested = load_and_parse_file(nested_path, depth + 1);

            if (!nested) {
                stage_error(STAGE_PARSER, imp->loc,
                    "could not load module '%s' (file: %s)", imp->module_name, nested_path);
                free(nested_path);
                continue;
            }

            // merge functions from nested include into this program
            for (int j = 0; j < nested->func_count; j++) {
                bool should_include = false;

                if (imp->type == IMPORT_ALL) {
                    should_include = true;
                } else {
                    // iMPORT_SPECIFIC — only include matching function
                    should_include = (strcmp(nested->functions[j]->signature->name, imp->function_name) == 0);
                }

                if (should_include) {
                    // grow the functions array if needed
                    int new_count = prog->func_count + 1;
                    prog->functions = realloc(prog->functions, sizeof(Func*) * new_count);
                    prog->functions[prog->func_count] = nested->functions[j];
                    prog->func_count = new_count;
                }
            }

            // check if IMPORT_SPECIFIC found its function
            if (imp->type == IMPORT_SPECIFIC) {
                bool found = false;
                for (int j = 0; j < nested->func_count; j++) {
                    if (strcmp(nested->functions[j]->signature->name, imp->function_name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    stage_error(STAGE_PARSER, imp->loc,
                        "function '%s' not found in module '%s'", imp->function_name, imp->module_name);
                }
            }

            free(nested_path);
        }
        free(dir);
    }

    return prog;
}

void process_file_includes(Program* prog, const char* source_file) {
    if (!prog || !prog->imports) return;

    reset_loaded_files();
    mark_file_loaded(source_file); // dont let the main file include itself

    char* source_dir = get_directory(source_file);

    for (int i = 0; i < prog->imports->import_count; i++) {
        IncludeStmt* imp = prog->imports->imports[i];

        // skip std.* imports — theyre handled by the existing system
        if (strncmp(imp->module_name, "std.", 4) == 0) continue;

        char* file_path = resolve_module_path(imp->module_name, source_dir);
        Program* included = load_and_parse_file(file_path, 0);

        if (!included) {
            stage_error(STAGE_PARSER, imp->loc,
                "could not load module '%s' (file: %s)", imp->module_name, file_path);
            free(file_path);
            continue;
        }

        // merge functions based on import type
        for (int j = 0; j < included->func_count; j++) {
            bool should_include = false;

            if (imp->type == IMPORT_ALL) {
                should_include = true;
            } else {
                should_include = (strcmp(included->functions[j]->signature->name, imp->function_name) == 0);
            }

            if (should_include) {
                // check for duplicate function (same name + param count already exists)
                bool duplicate = false;
                for (int k = 0; k < prog->func_count; k++) {
                    if (strcmp(prog->functions[k]->signature->name, included->functions[j]->signature->name) == 0 &&
                        prog->functions[k]->signature->paramNum == included->functions[j]->signature->paramNum) {
                        stage_error(STAGE_PARSER, imp->loc,
                            "duplicate function '%s' — already defined or imported",
                            included->functions[j]->signature->name);
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate) {
                    int new_count = prog->func_count + 1;
                    prog->functions = realloc(prog->functions, sizeof(Func*) * new_count);
                    prog->functions[prog->func_count] = included->functions[j];
                    prog->func_count = new_count;
                }
            }
        }

        // verify IMPORT_SPECIFIC found its target
        if (imp->type == IMPORT_SPECIFIC) {
            bool found = false;
            for (int j = 0; j < included->func_count; j++) {
                if (strcmp(included->functions[j]->signature->name, imp->function_name) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                stage_error(STAGE_PARSER, imp->loc,
                    "function '%s' not found in module '%s'", imp->function_name, imp->module_name);
            }
        }

        free(file_path);
    }

    free(source_dir);
}
