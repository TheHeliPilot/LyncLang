#include "../../src/error.h"
#include "../../src/lexer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ErrorCollector *g_error_collector = NULL;
bool g_trace_mode = true;
int g_trace_depth = 0;

typedef struct {
  char *input;
  TokenType *expected_types;
  int expected_count;
  const char *test_name;
} LexerTestCase;

void run_test(LexerTestCase test) {
  printf("Running test: %s... ", test.test_name);

  // Initialize error collector (mocked or real)
  g_error_collector = init_error_collector();

  TokenList *tokens = tokenize(test.input, "test.lync");
  token_list_print(tokens);

  // lexer adds EOF so we check count == expected_count + 1
  if (tokens->count != test.expected_count + 1) {
    printf("FAILED\n");
    printf("  Expected %d tokens, got %zu\n", test.expected_count + 1,
           tokens->count);
    token_list_print(tokens);
    exit(1);
  }

  for (int i = 0; i < test.expected_count; i++) {
    Token *t = token_list_at(tokens, i);
    if (t->type != test.expected_types[i]) {
      printf("FAILED\n");
      printf("  Token %d: Expected %s, got %s\n", i + 1,
             token_type_name(test.expected_types[i]), token_type_name(t->type));
      token_list_print(tokens);
      exit(1);
    }
  }

  // Check last token is EOF
  Token *last = token_list_at(tokens, tokens->count - 1);
  if (last->type != EOF_T) {
    printf("FAILED\n");
    printf("  Last token should be EOF, got %s\n", token_type_name(last->type));
    exit(1);
  }

  if (has_errors(g_error_collector)) {
    printf("FAILED (Lexer reported errors)\n");
    print_messages(g_error_collector);
    exit(1);
  }

  token_list_free(tokens);
  free_error_collector(g_error_collector);
  printf("PASSED\n");
}

int main() {
  printf("=== Running Lexer Tests ===\n");

  LexerTestCase test_cases[] = {
      {
          .input = "123 456",
          .expected_types = (TokenType[]){INT_LIT_T, INT_LIT_T},
          .expected_count = 2,
          .test_name = "Integers",
      },
      {.input = "123.45 0.0",
       .expected_types = (TokenType[]){FLOAT_LIT_T, FLOAT_LIT_T},
       .expected_count = 2,
       .test_name = "Floats"},
      {.input = "\"hello\" 'c'",
       .expected_types = (TokenType[]){STR_LIT_T, CHAR_LIT_T},
       .expected_count = 2,
       .test_name = "Strings and Chars"},
      {
          .input = "if else while do for return int bool void",
          .expected_types =
              (TokenType[]){IF_T, ELSE_T, WHILE_T, DO_T, FOR_T, RETURN_T,
                            INT_KEYWORD_T, BOOL_KEYWORD_T, VOID_KEYWORD_T},
          .expected_count = 9,
          .test_name = "Keywords",
      },
      {
          .input = "+ - * / = == != < > <= >= && || ! ; : , .",
          .expected_types =
              (TokenType[]){PLUS_T, MINUS_T, STAR_T, SLASH_T, EQUALS_T,
                            DOUBLE_EQUALS_T, NOT_EQUALS_T, LESS_T, MORE_T,
                            LESS_EQUALS_T, MORE_EQUALS_T, AND_T, OR_T,
                            NEGATION_T, SEMICOLON_T, COLON_T, COMMA_T, DOT_T},
          .expected_count = 18,
          .test_name = "Operators and Punctuation",
      },
      {
          .input = "( ) { } [ ]",
          .expected_types = (TokenType[]){L_PAREN_T, R_PAREN_T, L_BRACE_T,
                                          R_BRACE_T, L_BRACKET_T, R_BRACKET_T},
          .expected_count = 6,
          .test_name = "Operators and Punctuation",
      },
      {
          .input = "myVar _privateVar _",
          .expected_types = (TokenType[]){VAR_T, VAR_T, UNDERSCORE_T},
          .expected_count = 3,
          .test_name = "Identifiers",
      },
      {
          .input = "123 // comment\n 456",
          .expected_types = (TokenType[]){INT_LIT_T, INT_LIT_T},
          .expected_count = 2,
          .test_name = "Single Line Comment",
      },
      {
          .input = "// some comments",
          .expected_types = NULL,
          .expected_count = 0,
          .test_name = "Single Line Comment 2",
      },

      {
          .input = "123 /* multiline \n comment */ 456",
          .expected_types = (TokenType[]){INT_LIT_T, INT_LIT_T},
          .expected_count = 2,
          .test_name = "Multi Line Comment",
      },
      {
          .input = "if (x == 10) { return true; }",
          .expected_types =
              (TokenType[]){IF_T, L_PAREN_T, VAR_T, DOUBLE_EQUALS_T, INT_LIT_T,
                            R_PAREN_T, L_BRACE_T, RETURN_T, BOOL_LIT_T,
                            SEMICOLON_T, R_BRACE_T},
          .expected_count = 11,
          .test_name = "Complex Expression",
      },
      {
          .input = "//DUMMY TOP LEVEL COMMENT\n"
                   "def factorial(n: int): int {\n"
                   "    if (n <= 1) {\n"
                   "        return 1;\n"
                   "    } else {\n"
                   "        return n * factorial(n - 1);\n"
                   "    }\n"
                   "}\n"
                   "\n"
                   "def main(): int {\n"
                   "    x: int = 5;\n"
                   "    result: int = factorial(x);\n"
                   "    return 0;\n"
                   "}\n",
          .expected_types =
              (TokenType[]){
                  DEF_KEYWORD_T, VAR_T,         L_PAREN_T,     VAR_T,
                  COLON_T,       INT_KEYWORD_T, R_PAREN_T,     COLON_T,
                  INT_KEYWORD_T, L_BRACE_T,     IF_T,          L_PAREN_T,
                  VAR_T,         LESS_EQUALS_T, INT_LIT_T,     R_PAREN_T,
                  L_BRACE_T,     RETURN_T,      INT_LIT_T,     SEMICOLON_T,
                  R_BRACE_T,     ELSE_T,        L_BRACE_T,     RETURN_T,
                  VAR_T,         STAR_T,        VAR_T,         L_PAREN_T,
                  VAR_T,         MINUS_T,       INT_LIT_T,     R_PAREN_T,
                  SEMICOLON_T,   R_BRACE_T,     R_BRACE_T,     DEF_KEYWORD_T,
                  VAR_T,         L_PAREN_T,     R_PAREN_T,     COLON_T,
                  INT_KEYWORD_T, L_BRACE_T,     VAR_T,         COLON_T,
                  INT_KEYWORD_T, EQUALS_T,      INT_LIT_T,     SEMICOLON_T,
                  VAR_T,         COLON_T,       INT_KEYWORD_T, EQUALS_T,
                  VAR_T,         L_PAREN_T,     VAR_T,         R_PAREN_T,
                  SEMICOLON_T,   RETURN_T,      INT_LIT_T,     SEMICOLON_T,
                  R_BRACE_T},
          .expected_count = 61,
          .test_name = "Functions",
      },
      {
          .input = "1 // comment\r 2",
          .expected_types = (TokenType[]){INT_LIT_T, INT_LIT_T},
          .expected_count = 2,
          .test_name = "Single Line Comment CR",
      },
      {
          .input = "1 // comment\r\n 2",
          .expected_types = (TokenType[]){INT_LIT_T, INT_LIT_T},
          .expected_count = 2,
          .test_name = "Single Line Comment CRLF",
      },
      {
          .input = "/* comment */",
          .expected_types = (TokenType[]){},
          .expected_count = 0,
          .test_name = "Block Comment Simple",
      },
      {
          .input = "/* comment */ x: int = 5;",
          .expected_types = (TokenType[]){VAR_T, COLON_T, INT_KEYWORD_T,
                                          EQUALS_T, INT_LIT_T, SEMICOLON_T},
          .expected_count = 6,
          .test_name = "Block Comment with code",
      },
      {
          .input = "print(\"Negation (bool):\", ! t);",
          .expected_types =
              (TokenType[]){VAR_T, L_PAREN_T, STR_LIT_T, COMMA_T, NEGATION_T,
                            VAR_T, R_PAREN_T, SEMICOLON_T},
          .expected_count = 8,
          .test_name = "Print expression",
      },

  };
  const int num_tests = 17;

  for (int i = 0; i < num_tests; i++) {
    run_test(test_cases[i]);
  }

  printf("\nAll tests passed successfully!\n");
  return 0;
}
