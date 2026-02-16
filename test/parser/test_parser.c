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
