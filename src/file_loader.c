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

// Configurable stdlib root (set via --stdlib=PATH). Falls back to a sibling
// `stdlib/` directory next to lync.exe — discovered lazily on first use so we
// don't depend on argv[0] being available.
static char* g_stdlib_dir = NULL;

void file_loader_set_stdlib_dir(const char* path) {
    if (g_stdlib_dir) free(g_stdlib_dir);
    g_stdlib_dir = path ? strdup(path) : NULL;
}

// Resolve a `std.<mod>` import to a real file path inside g_stdlib_dir.
// Returns NULL if no stdlib dir is configured or no matching file exists.
static char* try_stdlib_path(const char* module_name) {
    if (!g_stdlib_dir || !module_name) return NULL;
    // Convention: `std.list` -> "<stdlib>/std.list.lync" (kept flat — simpler
    // than a nested std/ subdir and matches the on-disk layout we ship).
    size_t need = strlen(g_stdlib_dir) + 1 + strlen(module_name) + 6;
    char* path = malloc(need);
    snprintf(path, need, "%s/%s.lync", g_stdlib_dir, module_name);
    FILE* probe = fopen(path, "r");
    if (!probe) { free(path); return NULL; }
    fclose(probe);
    return path;
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
            // std.* imports: try the configured stdlib dir first; if a file
            // exists there, load it like any normal module. Otherwise skip
            // (these are the inline-codegen builtins like std.io).
            if (strncmp(imp->module_name, "std.", 4) == 0) {
                char* sp = try_stdlib_path(imp->module_name);
                if (!sp) continue;
                Program* nested = load_and_parse_file(sp, depth + 1);
                if (nested) {
                    // Merge structs (templates included), extern blocks, and
                    // functions. The std modules are treated as IMPORT_ALL.
                    for (int j = 0; j < nested->struct_count; ++j) {
                        prog->structs = realloc(prog->structs,
                            sizeof(StructDecl*) * (prog->struct_count + 1));
                        prog->structs[prog->struct_count++] = nested->structs[j];
                    }
                    for (int j = 0; j < nested->ext_block_count; ++j) {
                        prog->externBlocks = realloc(prog->externBlocks,
                            sizeof(ExternBlock*) * (prog->ext_block_count + 1));
                        prog->externBlocks[prog->ext_block_count++] = nested->externBlocks[j];
                    }
                    for (int j = 0; j < nested->func_count; ++j) {
                        prog->functions = realloc(prog->functions,
                            sizeof(Func*) * (prog->func_count + 1));
                        prog->functions[prog->func_count++] = nested->functions[j];
                    }
                }
                free(sp);
                continue;
            }

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

        // std.* imports are normally inlined by codegen (std.io). For modules
        // shipped as .lync files in the stdlib dir (std.math, std.string,
        // std.list, ...), load and merge them like any other file so user
        // code can `include std.math.*;` and get its templates and helpers.
        // If no backing file exists, fall through to the legacy skip — the
        // import is then assumed to be a codegen builtin.
        if (strncmp(imp->module_name, "std.", 4) == 0) {
            char* sp = try_stdlib_path(imp->module_name);
            if (!sp) continue;
            Program* nested = load_and_parse_file(sp, 0);
            free(sp);
            if (nested) {
                for (int j = 0; j < nested->struct_count; ++j) {
                    prog->structs = realloc(prog->structs,
                        sizeof(StructDecl*) * (prog->struct_count + 1));
                    prog->structs[prog->struct_count++] = nested->structs[j];
                }
                for (int j = 0; j < nested->ext_block_count; ++j) {
                    prog->externBlocks = realloc(prog->externBlocks,
                        sizeof(ExternBlock*) * (prog->ext_block_count + 1));
                    prog->externBlocks[prog->ext_block_count++] = nested->externBlocks[j];
                }
                for (int j = 0; j < nested->func_count; ++j) {
                    bool should_include = (imp->type == IMPORT_ALL) ||
                        (strcmp(nested->functions[j]->signature->name, imp->function_name) == 0);
                    if (!should_include) continue;
                    prog->functions = realloc(prog->functions,
                        sizeof(Func*) * (prog->func_count + 1));
                    prog->functions[prog->func_count++] = nested->functions[j];
                }
            }
            continue;
        }

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

        // Merge struct decls (where [Component] decls live) for IMPORT_ALL.
        // Without this, `include Foo.*;` would only pull in Foo's functions
        // and silently drop its components - the Zues plugin then sees zero
        // [Component]s and emits no project entry, so the DLL has no
        // zues_project_entry symbol and the editor refuses to load it.
        if (imp->type == IMPORT_ALL && included->struct_count > 0) {
            for (int j = 0; j < included->struct_count; j++) {
                bool duplicate = false;
                for (int k = 0; k < prog->struct_count; k++) {
                    if (strcmp(prog->structs[k]->name,
                               included->structs[j]->name) == 0) {
                        stage_error(STAGE_PARSER, imp->loc,
                            "duplicate struct '%s' - already defined or imported",
                            included->structs[j]->name);
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    int new_count = prog->struct_count + 1;
                    prog->structs = realloc(prog->structs,
                                             sizeof(StructDecl*) * new_count);
                    prog->structs[prog->struct_count] = included->structs[j];
                    prog->struct_count = new_count;
                }
            }
        }

        // Same for extern blocks (the prelude header decls) - some users
        // put their `extern <stddef.h> { ... }` in a shared file and
        // include it.
        if (imp->type == IMPORT_ALL && included->ext_block_count > 0) {
            for (int j = 0; j < included->ext_block_count; j++) {
                int new_count = prog->ext_block_count + 1;
                prog->externBlocks = realloc(prog->externBlocks,
                                              sizeof(ExternBlock*) * new_count);
                prog->externBlocks[prog->ext_block_count] =
                    included->externBlocks[j];
                prog->ext_block_count = new_count;
            }
        }

        free(file_path);
    }

    // Now that every imported module's structs, functions, and templates
    // have been merged into `prog`, run a strict template drain to realize
    // any pendings that were left unresolved by parseProgram (e.g. uses of
    // `min<int>` from the main file when min<T> lives in std.math).
    tpl_drain_pending(prog, true);

    free(source_dir);
}
