#include "common.h"
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "analyzer.h"
#include "optimizer.h"
#include "file_loader.h"
#include "plugin.h"

#ifdef _WIN32
#include <process.h>
#define NULL_REDIRECT ">nul 2>&1"
#define EXE_EXT ".exe"
#else
#include <sys/wait.h>
#define NULL_REDIRECT ">/dev/null 2>&1"
#define EXE_EXT ""
#endif

//global state definitions
ErrorCollector* g_error_collector = nullptr;
bool g_trace_mode = false;
int g_trace_depth = 0;

//--- c compiler detection ---

static const char* find_c_compiler(void) {
    const char* compilers[] = {
#ifdef _WIN32
        "gcc", "clang", "cl",
#else
        "cc", "gcc", "clang",
#endif
    };
    int count = sizeof(compilers) / sizeof(compilers[0]);

    for (int i = 0; i < count; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s --version %s", compilers[i], NULL_REDIRECT);
        if (system(cmd) == 0) {
            return compilers[i];
        }
    }
    return nullptr;
}

static char* replace_extension(const char* path, const char* new_ext) {
    size_t len = strlen(path);
    const char* dot = nullptr;

    //find the last . that comes after the last path separator
    for (size_t i = len; i > 0; i--) {
        if (path[i - 1] == '.' && dot == nullptr) {
            dot = &path[i - 1];
        }
        if (path[i - 1] == '/' || path[i - 1] == '\\') {
            break;
        }
    }

    size_t base_len = dot ? (size_t)(dot - path) : len;
    size_t ext_len = strlen(new_ext);
    char* result = malloc(base_len + ext_len + 1);
    memcpy(result, path, base_len);
    memcpy(result + base_len, new_ext, ext_len);
    result[base_len + ext_len] = '\0';
    return result;
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] [input_file]\n", program_name);
    fprintf(stderr, "       %s run [options] [input_file]\n", program_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>      Output executable name\n");
    fprintf(stderr, "  -S             Emit assembly instead of executable\n");
    fprintf(stderr, "  --emit-c       Keep the intermediate .c file\n");
    fprintf(stderr, "  -trace         Enable trace/debug output\n");
    fprintf(stderr, "  -no-color      Disable colored output\n");
    fprintf(stderr, "  -O0            No optimization (default)\n");
    fprintf(stderr, "  -O1            Basic optimizations (constant folding)\n");
    fprintf(stderr, "  -O2            More optimizations (dead code elimination)\n");
    fprintf(stderr, "  -O3            All optimizations (including inlining)\n");
    fprintf(stderr, "  -Os            Optimize for size\n");
    fprintf(stderr, "  -h, --help     Show this help message\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no input file is specified, defaults to ../test.lync\n");
}

int main(int argc, char** argv) {
    const char* input_file = nullptr;
    const char* exe_output = nullptr;  // -o flag: executable name
    bool no_color = false;
    bool emit_c = false;
    bool emit_asm = false;
    bool run_mode = false;

    int opt_level = 0;
    bool opt_size = false;

    // Collect --plugin=path arguments. Loaded after we have an output FILE
    // so the LyncContext can be passed to hooks; the path list is just
    // stored for now.
    const char* plugin_paths[16];
    int         plugin_path_count = 0;

    // --target=exe (default) or --target=dll. DLL skips main, passes
    // -shared to the C compiler, and outputs .dll/.so/.dylib.
    bool target_is_dll = false;

    // --include=path repeated; passed as -I to the C compiler. Lets DLL
    // builds pull in the host's project_api.h without baking the path.
    const char* include_paths[16];
    int         include_path_count = 0;

    // --prelude=path: a .lync file prepended to every input file before
    // parsing. Used to ship API extern blocks (e.g. zues_api.lync) so users
    // don't repeat the same boilerplate in every project source.
    const char* prelude_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "run") == 0 && !input_file && !run_mode) {
            run_mode = true;
        } else if (strcmp(argv[i], "-trace") == 0 || strcmp(argv[i], "--trace") == 0) {
            g_trace_mode = true;
        } else if (strcmp(argv[i], "-no-color") == 0 || strcmp(argv[i], "--no-color") == 0) {
            no_color = true;
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            emit_c = true;
        } else if (strcmp(argv[i], "-S") == 0) {
            emit_asm = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            exe_output = argv[++i];
        } else if (strncmp(argv[i], "--plugin=", 9) == 0) {
            if (plugin_path_count < 16) {
                plugin_paths[plugin_path_count++] = argv[i] + 9;
            }
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            const char* t = argv[i] + 9;
            if (strcmp(t, "dll") == 0 || strcmp(t, "shared") == 0) {
                target_is_dll = true;
            } else if (strcmp(t, "exe") == 0) {
                target_is_dll = false;
            } else {
                fprintf(stderr, "Unknown --target value: '%s' (expected exe|dll)\n", t);
                return 1;
            }
        } else if (strncmp(argv[i], "--include=", 10) == 0) {
            if (include_path_count < 16) {
                include_paths[include_path_count++] = argv[i] + 10;
            }
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            if (include_path_count < 16) {
                include_paths[include_path_count++] = argv[++i];
            }
        } else if (strncmp(argv[i], "--prelude=", 10) == 0) {
            prelude_path = argv[i] + 10;
        } else if (strncmp(argv[i], "--stdlib=", 9) == 0) {
            // Where to look for std.<module>.lync files. If unset, the file
            // loader falls back to a directory next to lync.exe.
            extern void file_loader_set_stdlib_dir(const char*);
            file_loader_set_stdlib_dir(argv[i] + 9);
        }

        else if (strcmp(argv[i], "-O0") == 0) opt_level = 0;
        else if (strcmp(argv[i], "-O1") == 0) opt_level = 1;
        else if (strcmp(argv[i], "-O2") == 0) opt_level = 2;
        else if (strcmp(argv[i], "-O3") == 0) opt_level = 3;
        else if (strcmp(argv[i], "-Os") == 0) { opt_level = 2; opt_size = true; }

        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!input_file) input_file = "../test.lync";

    //compute output paths. DLL target uses the platform's shared-library
    //extension; exe target uses EXE_EXT.
    char* c_file = replace_extension(input_file, ".c");
    char* exe_file;
    if (exe_output) {
        exe_file = strdup(exe_output);
    } else {
#if defined(_WIN32)
        const char* default_ext = target_is_dll ? ".dll" : EXE_EXT;
#elif defined(__APPLE__)
        const char* default_ext = target_is_dll ? ".dylib" : EXE_EXT;
#else
        const char* default_ext = target_is_dll ? ".so"   : EXE_EXT;
#endif
        exe_file = replace_extension(input_file, default_ext);
    }

    //find a C compiler
    const char* compiler = find_c_compiler();
    if (!compiler) {
        fprintf(stderr, "Error: no C compiler found. Install gcc, clang, or MSVC and ensure it's on your PATH.\n");
        free(c_file);
        free(exe_file);
        return 1;
    }
    stage_trace(STAGE_CODEGEN, "using C compiler: %s", compiler);

    //initialize error collector
    g_error_collector = init_error_collector();
    if (no_color) g_error_collector->use_color = false;

    // Optional prelude. Read it once + prepend to the input. Same lexer
    // pass handles the combined source — line numbers in errors will be
    // the combined-buffer offsets, but the prelude is meant to be
    // boilerplate the user never edits, so the offset cost is acceptable.
    char*  prelude_buf  = nullptr;
    long   prelude_size = 0;
    if (prelude_path) {
        FILE* pf = fopen(prelude_path, "r");
        if (!pf) {
            fprintf(stderr, "Error: cannot open prelude '%s'\n", prelude_path);
            free(c_file);
            free(exe_file);
            return 1;
        }
        fseek(pf, 0, SEEK_END);
        prelude_size = ftell(pf);
        fseek(pf, 0, SEEK_SET);
        prelude_buf = malloc(prelude_size + 2);
        size_t pr = fread(prelude_buf, 1, prelude_size, pf);
        prelude_buf[pr] = '\n';
        prelude_buf[pr + 1] = '\0';
        fclose(pf);
        prelude_size = (long)(pr + 1);
        stage_trace(STAGE_LEXER, "loaded prelude '%s' (%ld bytes)",
                    prelude_path, prelude_size);
    }

    //read input file
    FILE* file = fopen(input_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open '%s'\n", input_file);
        free(c_file);
        free(exe_file);
        free(prelude_buf);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* user_code = malloc(file_size + 1);
    size_t bytes_read = fread(user_code, 1, file_size, file);
    user_code[bytes_read] = '\0';
    fclose(file);

    // Concat prelude + user source. The prelude is fully tokenised first,
    // so any extern decls / includes in it land before the user's code in
    // the AST — exactly like the user typed them at the top of their file.
    char* code;
    if (prelude_buf) {
        code = malloc(prelude_size + bytes_read + 1);
        memcpy(code, prelude_buf, prelude_size);
        memcpy(code + prelude_size, user_code, bytes_read);
        code[prelude_size + bytes_read] = '\0';
        free(user_code);
        free(prelude_buf);
        prelude_buf = nullptr;
    } else {
        code = user_code;
    }

    //--- lexer ---
    stage_trace_enter(STAGE_LEXER, "starting lexical analysis");
    int token_count;
    Token* tokens = tokenize(code, &token_count, input_file);
    stage_trace_exit(STAGE_LEXER, "completed, %d tokens", token_count);
    print_tokens(tokens, token_count);

    //check for lexer errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    //--- parser ---
    stage_trace_enter(STAGE_PARSER, "starting parsing");
    Parser parser = {
            .tokens = tokens,
            .count = token_count,
            .size = token_count,
            .pos = 0
    };

    Program* program = parseProgram(&parser);
    stage_trace_exit(STAGE_PARSER, "parsed %d functions, %d imports",
        program->func_count, program->imports->import_count);
    print_ast(program->functions, program->func_count);

    //check for parser errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    //--- file includes ---
    stage_trace_enter(STAGE_PARSER, "processing file includes");
    process_file_includes(program, input_file);
    stage_trace_exit(STAGE_PARSER, "file includes processed, now %d functions", program->func_count);

    //check for include errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // ---- Plugin pre-analyze pass (v2) -----------------------------------
    // Load plugins early so they can synthesize Lync decls (extern blocks,
    // helpers) BEFORE the analyzer runs. The plugin context's output file
    // is set later, when codegen opens it. Any post-analysis dispatch_decls
    // call below uses the same context.
    LyncContext* plugin_ctx = NULL;
    if (plugin_path_count > 0) {
        for (int i = 0; i < plugin_path_count; ++i) plugin_load(plugin_paths[i]);
        plugin_ctx = plugin_context_create(NULL);
        plugin_dispatch_load(plugin_ctx);
        plugin_dispatch_decls_pre_analyze(plugin_ctx, program);

        // Pull whatever lync source the plugins emitted. Parse it as a
        // standalone fragment; merge its extern blocks + functions into
        // the user program so the analyzer sees them as if hand-written.
        char* synth = plugin_context_take_lync_decls(plugin_ctx);
        if (synth && *synth) {
            stage_trace_enter(STAGE_PARSER, "parsing plugin-synthesized Lync decls");
            int synth_token_count;
            Token* synth_tokens = tokenize(synth, &synth_token_count, "<plugin-synth>");
            if (!has_errors(g_error_collector)) {
                Parser synth_parser = {
                    .tokens = synth_tokens,
                    .count  = synth_token_count,
                    .size   = synth_token_count,
                    .pos    = 0
                };
                Program* synth_prog = parseProgram(&synth_parser);
                if (!has_errors(g_error_collector) && synth_prog) {
                    // Merge extern blocks. Functions / structs from synth
                    // would also work via the same realloc pattern but
                    // the v2 use case is extern decls only.
                    for (int i = 0; i < synth_prog->ext_block_count; ++i) {
                        int new_count = program->ext_block_count + 1;
                        program->externBlocks = realloc(program->externBlocks,
                                                        sizeof(ExternBlock*) * new_count);
                        program->externBlocks[program->ext_block_count] =
                            synth_prog->externBlocks[i];
                        program->ext_block_count = new_count;
                    }
                    // Functions too (in case a plugin emits helper fns).
                    for (int i = 0; i < synth_prog->func_count; ++i) {
                        int new_count = program->func_count + 1;
                        program->functions = realloc(program->functions,
                                                     sizeof(Func*) * new_count);
                        program->functions[program->func_count] =
                            synth_prog->functions[i];
                        program->func_count = new_count;
                    }
                }
            }
            stage_trace_exit(STAGE_PARSER, "plugin-synth parse done");
            free(synth);
        }
        if (has_errors(g_error_collector)) {
            print_messages(g_error_collector);
            free_error_collector(g_error_collector);
            free(code); free(c_file); free(exe_file);
            return 1;
        }
    }

    //--- analyzer ---
    stage_trace_enter(STAGE_ANALYZER, "starting semantic analysis");
    analyze_program(program);
    stage_trace_exit(STAGE_ANALYZER, "analysis complete");

    //check for analyzer errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    //--- optimizer ---
    if (opt_level > 0) {
        stage_trace_enter(STAGE_OPTIMIZER, "starting optimizations");

        OptimizationLevel level = OPT_NONE;
        if (opt_level >= 1) level |= OPT_CONST_FOLD;
        if (opt_level >= 2) level |= OPT_DEAD_CODE | OPT_PEEPHOLE;
        if (opt_level >= 3) level |= OPT_INLINE;
        if (opt_size) {
            level &= ~OPT_INLINE;  // inlining increases size
        }

        optimize_program(program->functions, program->func_count, level);

        //re-run analysis after optimizations? Not sure if needed?
        //analyze_program(program, func_count);

        stage_trace_exit(STAGE_OPTIMIZER, "optimizations complete");
    }

    //--- codegen ---
    stage_trace_enter(STAGE_CODEGEN, "starting code generation");
    FILE *output = fopen(c_file, "w");
    if (!output) {
        fprintf(stderr, "Error: Could not open output file '%s'\n", c_file);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // ---- Plugin hooks: post-analysis C codegen pass ----
    // Plugins were loaded earlier (so their pre-analyze hooks could
    // synthesize Lync decls). Now bind the output FILE* and dispatch the
    // C-emission walk. Buffers flush after generate_code so plugin code
    // lands at the END of the .c file, never interleaved with user code.
    if (plugin_ctx) {
        plugin_context_set_output(plugin_ctx, output);
        plugin_dispatch_decls(plugin_ctx, program);
    }

    generate_code(program, output);

    if (plugin_ctx) {
        plugin_dispatch_finalize(plugin_ctx);
        plugin_dispatch_unload(plugin_ctx);
        plugin_context_destroy(plugin_ctx);
        plugin_shutdown();
    }

    fclose(output);
    stage_trace_exit(STAGE_CODEGEN, "wrote %s", c_file);

    //print any warnings
    print_messages(g_error_collector);

    stage_trace_enter(STAGE_CODEGEN, "invoking C backend");
    char cmd[4096];
    int  off = 0;
    off += snprintf(cmd + off, sizeof(cmd) - off,
                    "%s \"%s\"", compiler, c_file);

    // Include paths from --include= / -I flags. Forwarded as -I path so
    // the C compiler can find host headers (e.g. zues/project_api.h when
    // compiling a Zues project DLL).
    for (int i = 0; i < include_path_count && off < (int)sizeof(cmd) - 256; ++i) {
        off += snprintf(cmd + off, sizeof(cmd) - off,
                        " -I \"%s\"", include_paths[i]);
    }

    if (target_is_dll) {
        // -shared works for both clang and gcc on Windows + POSIX.
        // (cl /LD would need separate handling; we don't auto-detect cl yet.)
        off += snprintf(cmd + off, sizeof(cmd) - off, " -shared");
    }

    off += snprintf(cmd + off, sizeof(cmd) - off,
                    " -o \"%s\"", exe_file);
    stage_trace(STAGE_CODEGEN, "running: %s", cmd);

    int cc_result = system(cmd);
    stage_trace_exit(STAGE_CODEGEN, "C compiler exited with %d", cc_result);

    if (cc_result != 0) {
        fprintf(stderr, "\nError: C compiler failed (exit code %d)\n", cc_result);
        fprintf(stderr, "Intermediate file kept: %s\n", c_file);
        //dont delete .c file on failure
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    //clean up intermediate .c file (unless --emit-c)
    if (!emit_c) {
        remove(c_file);
    }

    //--- success or run ---
    int exit_code = 0;

    if (target_is_dll && run_mode) {
        fprintf(stderr, "warning: 'run' has no effect with --target=dll; ignoring.\n");
        run_mode = false;
    }
    if (run_mode) {
        //run the compiled executable
        char run_cmd[2048];
#ifdef _WIN32
        snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", exe_file);
#else
        //prepend ./ if the path doesnt contain a separator
        if (strchr(exe_file, '/') == nullptr) {
            snprintf(run_cmd, sizeof(run_cmd), "./%s", exe_file);
        } else {
            snprintf(run_cmd, sizeof(run_cmd), "%s", exe_file);
        }
#endif
        stage_trace(STAGE_CODEGEN, "running: %s", run_cmd);
        int run_result = system(run_cmd);

#ifdef _WIN32
        exit_code = run_result;
#else
        exit_code = WIFEXITED(run_result) ? WEXITSTATUS(run_result) : run_result;
#endif
    } else {
        if (has_warnings(g_error_collector)) {
            printf("\nCompiled %s -> %s (%d warning%s)\n",
                   input_file, exe_file,
                   g_error_collector->warning_count,
                   g_error_collector->warning_count == 1 ? "" : "s");
        } else {
            printf("\nCompiled %s -> %s\n", input_file, exe_file);
        }
        if (emit_c) {
            printf("Kept intermediate: %s\n", c_file);
        }
    }

    free(tokens);
    free(code);
    free(c_file);
    free(exe_file);
    free_error_collector(g_error_collector);

    return exit_code;
}
