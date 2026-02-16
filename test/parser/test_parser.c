#include "../../src/error.h"
#include "../../src/lexer.h"
#include "../../src/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock globals
ErrorCollector *g_error_collector = NULL;
bool g_trace_mode = false;
int g_trace_depth = 0;

void run_test_file(const char *filepath) {
  printf("Testing %s... ", filepath);

  g_error_collector = init_error_collector();

  FILE *f = fopen(filepath, "rb");
  if (!f) {
    printf("FAILED. Could not open file: %s\n", filepath);
    free_error_collector(g_error_collector);
    return;
  }

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buffer = malloc(length + 1);
  unsigned long _ = fread(buffer, 1, length, f);
  buffer[length] = '\0';
  fclose(f);

  const int ERROR_TAG_LEN = 8;
  const char *ERROR_TAG = "//ERROR:";
  const int MAX_EXPECTED_ERRORS = 10;
  int error_count = 0;
  char *expected_errors[MAX_EXPECTED_ERRORS];
  char *current_pos = buffer;
  char *next_newline;

  while (current_pos < buffer + length) {
    next_newline = strchr(current_pos, '\n');
    // 2. Determine line boundary (handle the last line if no \n exists)
    char *line_end = next_newline ? next_newline : (buffer + length);
    size_t line_len = line_end - current_pos;
    if (line_len > 0) {
      if (strncmp(current_pos, ERROR_TAG, ERROR_TAG_LEN) == 0) {
        if (error_count == MAX_EXPECTED_ERRORS) {
          printf("Execed MAX_EXPECTED_ERRORS of %d \n", MAX_EXPECTED_ERRORS);
          exit(1);
        }
        char *error_message = current_pos + ERROR_TAG_LEN;
        expected_errors[error_count++] = error_message;
      }
    }
    if (!next_newline) {
      break; // We reached the end of the buffer
    }
    // move to next line
    current_pos = next_newline + 1;
  }

  TokenList *tokens = tokenize(buffer, filepath);

  if (has_errors(g_error_collector)) {
    printf("FAILED (Lexer Error)\n");
    print_messages(g_error_collector);
    free(buffer);
    token_list_free(tokens);
    free_error_collector(g_error_collector);
    exit(1);
  }

  Parser parser = {.tokens = tokens->tokens,
                   .count = tokens->count,
                   .size = tokens->count,
                   .pos = 0};

  Program *prog = parseProgram(&parser);
  print_ast(prog->functions, prog->func_count);

  if (has_errors(g_error_collector) || !prog) {
    printf("FAILED (Parser Error)\n");
    print_messages(g_error_collector);
    exit(1);
  } else {
    printf("PASSED\n");
  }

  // Cleanup
  free(buffer);
  token_list_free(tokens);
  free_error_collector(g_error_collector);
}

int main() {
  printf("=== Running Parser Tests ===\n");
  FILE *manifest = fopen("test/source_files/test_manifest.txt", "r");
  if (!manifest) {
    printf("FAILED. Manifest file not found\n");
    exit(1);
  }
  char filename[256];
  while (fgets(filename, sizeof(filename), manifest)) {
    printf("filename: %s\n", filename);
    filename[strcspn(filename, "\n")] = 0;

    char path_buffer[512];
    snprintf(path_buffer, sizeof(path_buffer), "test/source_files/%s",
             filename);
    run_test_file(path_buffer);
  }
  fclose(manifest);

  printf("\nAll parser tests passed successfully!\n");
  return 0;
}
