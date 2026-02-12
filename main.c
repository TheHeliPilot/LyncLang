#include "common.h"
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "analyzer.h"
#include "optimizer.h"

#ifdef _WIN32
#include <process.h>
#define NULL_REDIRECT ">nul 2>&1"
#define EXE_EXT ".exe"
#else
#include <sys/wait.h>
#define NULL_REDIRECT ">/dev/null 2>&1"
#define EXE_EXT ""
#endif

// Global state definitions
ErrorCollector* g_error_collector = nullptr;
bool g_trace_mode = false;
int g_trace_depth = 0;

// --- C compiler detection ---

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

// --- Path utilities ---

// Replace .lync extension with a new extension, or append if no .lync found.
// Returns a malloc'd string — caller must free.
static char* replace_extension(const char* path, const char* new_ext) {
    size_t len = strlen(path);
    const char* dot = nullptr;

    // Find the last '.' that comes after the last path separator
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

// --- Help ---

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
        }
            // NEW: Optimization flags
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

    // Set defaults
    if (!input_file) input_file = "../test.lync";

    // Compute output paths
    char* c_file = replace_extension(input_file, ".c");
    char* exe_file;
    if (exe_output) {
        exe_file = strdup(exe_output);
    } else {
        exe_file = replace_extension(input_file, EXE_EXT);
    }

    // Find a C compiler
    const char* compiler = find_c_compiler();
    if (!compiler) {
        fprintf(stderr, "Error: no C compiler found. Install gcc, clang, or MSVC and ensure it's on your PATH.\n");
        free(c_file);
        free(exe_file);
        return 1;
    }
    stage_trace(STAGE_CODEGEN, "using C compiler: %s", compiler);

    // Initialize error collector
    g_error_collector = init_error_collector();
    if (no_color) g_error_collector->use_color = false;

    // Read input file
    FILE* file = fopen(input_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open '%s'\n", input_file);
        free(c_file);
        free(exe_file);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* code = malloc(file_size + 1);
    size_t bytes_read = fread(code, 1, file_size, file);
    code[bytes_read] = '\0';
    fclose(file);

    // --- LEXER ---
    stage_trace_enter(STAGE_LEXER, "starting lexical analysis");
    int token_count;
    Token* tokens = tokenize(code, &token_count, input_file);
    stage_trace_exit(STAGE_LEXER, "completed, %d tokens", token_count);
    print_tokens(tokens, token_count);

    // Check for lexer errors (already collected, just check)
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // --- PARSER ---
    stage_trace_enter(STAGE_PARSER, "starting parsing");
    Parser parser = {
            .tokens = tokens,
            .count = token_count,
            .size = token_count,
            .pos = 0
    };

    int func_count;
    Func** program = parseProgram(&parser, &func_count);
    stage_trace_exit(STAGE_PARSER, "parsed %d functions", func_count);
    print_ast(program, func_count);

    // Check for parser errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // --- ANALYZER ---
    stage_trace_enter(STAGE_ANALYZER, "starting semantic analysis");
    analyze_program(program, func_count);
    stage_trace_exit(STAGE_ANALYZER, "analysis complete");

    // Check for analyzer errors
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // --- OPTIMIZER ---
    if (opt_level > 0) {
        stage_trace_enter(STAGE_OPTIMIZER, "starting optimizations");

        OptimizationLevel level = OPT_NONE;
        if (opt_level >= 1) level |= OPT_CONST_FOLD;
        if (opt_level >= 2) level |= OPT_DEAD_CODE | OPT_PEEPHOLE;
        if (opt_level >= 3) level |= OPT_INLINE;
        if (opt_size) {
            level &= ~OPT_INLINE;  // Inlining increases size
        }

        optimize_program(program, func_count, level);

        // Re-run analysis after optimizations? Optional
        // analyze_program(program, func_count);

        stage_trace_exit(STAGE_OPTIMIZER, "optimizations complete");
    }

    // --- CODEGEN ---
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

    generate_code(program, func_count, output);
    fclose(output);
    stage_trace_exit(STAGE_CODEGEN, "wrote %s", c_file);

    // Print any warnings
    print_messages(g_error_collector);

    // --- BACKEND: invoke C compiler ---
    stage_trace_enter(STAGE_CODEGEN, "invoking C backend");
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s \"%s\" -o \"%s\"", compiler, c_file, exe_file);
    stage_trace(STAGE_CODEGEN, "running: %s", cmd);

    int cc_result = system(cmd);
    stage_trace_exit(STAGE_CODEGEN, "C compiler exited with %d", cc_result);

    if (cc_result != 0) {
        fprintf(stderr, "\nError: C compiler failed (exit code %d)\n", cc_result);
        fprintf(stderr, "Intermediate file kept: %s\n", c_file);
        // Don't delete .c file on failure — user needs it to debug
        free_error_collector(g_error_collector);
        free(code);
        free(c_file);
        free(exe_file);
        return 1;
    }

    // Clean up intermediate .c file (unless --emit-c)
    if (!emit_c) {
        remove(c_file);
    }

    // --- SUCCESS or RUN ---
    int exit_code = 0;

    if (run_mode) {
        // Run the compiled executable
        char run_cmd[2048];
#ifdef _WIN32
        snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", exe_file);
#else
        // Prepend ./ if the path doesn't contain a separator
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
        // Print success message
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
