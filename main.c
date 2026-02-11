#include "common.h"
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "analyzer.h"

// Global state definitions
ErrorCollector* g_error_collector = NULL;
bool g_trace_mode = false;
int g_trace_depth = 0;

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] [input_file]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>      Write output to <file> (default: test.c)\n");
    fprintf(stderr, "  -trace         Enable trace/debug output\n");
    fprintf(stderr, "  -no-color      Disable colored output\n");
    fprintf(stderr, "  -h, --help     Show this help message\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If no input file is specified, defaults to test.cmm\n");
}

int main(int argc, char** argv) {
    const char* input_file = NULL;
    const char* output_file = NULL;
    bool no_color = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-trace") == 0 || strcmp(argv[i], "--trace") == 0) {
            g_trace_mode = true;
        } else if (strcmp(argv[i], "-no-color") == 0 || strcmp(argv[i], "--no-color") == 0) {
            no_color = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
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
    if (!input_file) input_file = "../test.cmm";
    if (!output_file) output_file = "../test.c";

    // Initialize error collector
    g_error_collector = init_error_collector();
    if (no_color) g_error_collector->use_color = false;

    // Read input file
    FILE* file = fopen(input_file, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open '%s'\n", input_file);
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
    Token* tokens = tokenize(code, &token_count);
    stage_trace_exit(STAGE_LEXER, "completed, %d tokens", token_count);
    print_tokens(tokens, token_count);

    // Check for lexer errors (already collected, just check)
    if (has_errors(g_error_collector)) {
        print_messages(g_error_collector);
        free_error_collector(g_error_collector);
        free(code);
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
        return 1;
    }

    // --- CODEGEN ---
    stage_trace_enter(STAGE_CODEGEN, "starting code generation");
    FILE *output = fopen(output_file, "w");
    if (!output) {
        fprintf(stderr, "Error: Could not open output file '%s'\n", output_file);
        free_error_collector(g_error_collector);
        free(code);
        return 1;
    }

    generate_code(program, func_count, output);
    fclose(output);
    stage_trace_exit(STAGE_CODEGEN, "wrote %s", output_file);

    // Print any warnings (if no errors)
    print_messages(g_error_collector);

    // Success message
    if (has_warnings(g_error_collector)) {
        printf("\nCompilation successful with %d warning%s.\n",
               g_error_collector->warning_count,
               g_error_collector->warning_count == 1 ? "" : "s");
    } else {
        printf("\nCompilation successful.\n");
    }

    // Cleanup
    for (int i = 0; i < token_count; i++) {
        if (tokens[i].value != NULL) {
            free(tokens[i].value);
        }
    }
    free(tokens);
    free(code);
    free_error_collector(g_error_collector);

    return 0;
}
