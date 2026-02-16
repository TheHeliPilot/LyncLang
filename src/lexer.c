// created by bucka on 2/9/2026.

#include "lexer.h"
#include "error.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern ErrorCollector *g_error_collector;
extern bool g_trace_mode;

typedef struct {
  const char *name;
  TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {{"if", IF_T},
                                   {"else", ELSE_T},
                                   {"int", INT_KEYWORD_T},
                                   {"char", CHAR_KEYWORD_T},
                                   {"void", VOID_KEYWORD_T},
                                   {"null", NULL_LIT_T},
                                   {"bool", BOOL_KEYWORD_T},
                                   {"string", STR_KEYWORD_T},
                                   {"def", DEF_KEYWORD_T},
                                   {"include", INCLUDE_T},
                                   {"extern", EXTERN_T},
                                   {"while", WHILE_T},
                                   {"do", DO_T},
                                   {"for", FOR_T},
                                   {"to", TO_T},
                                   {"return", RETURN_T},
                                   {"alloc", ALLOC_T},
                                   {"free", FREE_T},
                                   {"match", MATCH_T},
                                   {"some", SOME_T},
                                   {"own", OWN_T},
                                   {"ref", REF_T},
                                   {"const", CONST_T},
                                   {"float", FLOAT_KEYWORD_T},
                                   {"double", DOUBLE_KEYWORD_T},
                                   {"true", BOOL_LIT_T},
                                   {"false", BOOL_LIT_T},
                                   {"_", UNDERSCORE_T},
                                   {"_", UNDERSCORE_T}};

static const int NUM_KEYWORDS = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

int token_list_resize(TokenList *l) {
  l->capacity = l->capacity * TOKEN_CAPACITY_RESIZE_MULTIPLIER;
  Token *tokens = (Token *)realloc(l->tokens, l->capacity * sizeof(Token));
  if (!tokens) {
    return TOKEN_ALLOC_ERROR;
  }
  l->tokens = tokens;
  return TOKEN_ALLOC_SUCCESS;
}
TokenList *token_list_create() {
  TokenList *list = (TokenList *)malloc(sizeof(TokenList));
  if (!list) {
    return nullptr;
  }
  list->capacity = TOKEN_INIT_CAPACITY;
  list->count = 0;
  list->tokens = (Token *)malloc(list->capacity * sizeof(Token));
  if (!list->tokens) {
    free(list);
    return nullptr;
  }
  return list;
}
int token_list_append(TokenList *l, Token token) {
  if (l->count == l->capacity) {
    int res = token_list_resize(l);
    if (res == TOKEN_ALLOC_ERROR) {
      return TOKEN_ALLOC_ERROR;
    }
  }
  l->tokens[l->count] = token;
  l->count++;
  token_list_print(l);
  return TOKEN_APPEND_SUCCESS;
}

Token *token_list_at(TokenList *l, size_t i) {
  assert(i < l->count && "Token Index out of bounds");
  return &(l->tokens[i]);
}

void token_list_print(TokenList *l) {
  if (!g_trace_mode)
    return;

  fprintf(stderr, "=== TOKENS (%zu) ===\n", l->count);

  for (size_t i = 0; i < l->count; i++) {
    Token *t = token_list_at(l, i);

    fprintf(stderr, "[%3zu] [%s:%d:%d] ", i, t->filename, t->line, t->column);
    fprintf(stderr, "%-15s", token_type_name(t->type));

    switch (t->type) {
    case INT_LIT_T:
      fprintf(stderr, " = %lld", t->value.as_int);
      break;
    case FLOAT_LIT_T:
      fprintf(stderr, " = %f", t->value.as_float);
      break;
    case CHAR_LIT_T:
      fprintf(stderr, " = '%c'", t->value.as_char);
      break;
    case BOOL_LIT_T:
      fprintf(stderr, " = %s", t->value.as_bool ? "true" : "false");
      break;
    case STR_LIT_T:
    case VAR_T:
      if (t->value.as_string) {
        fprintf(stderr, " = \"%s\"", t->value.as_string);
      }
      break;
    case NULL_LIT_T:
      fprintf(stderr, " = null");
      break;
    default:
      break;
    }

    fprintf(stderr, "\n");
  }
  fprintf(stderr, "==================\n");
}
void token_list_free(TokenList *l) {
  free(l->tokens);
  free(l);
}

// TODO:
/* typedef struct { */
/*   const size_t cursor; */
/*   const size_t line; */
/*   const char *code; */
/*   const char *filename; */
/* } Lexer; */

TokenList *tokenize(char *code, const char *filename) {
  TokenList *token_list = token_list_create();

  int line = 1;
  int column = 1;

  int i = 0;

  while (code[i] != '\0') {
    char c = code[i];
    int start_col = column; // save column at start of token

    if (c == '\n') {
      line++;
      column = 1;
      i++;
      continue;
    }

    if (c == ' ' || c == '\t' || c == '\r') {
      column++;
      i++;
      continue;
    }

    // number literals (int or float/double)
    if (code[i] >= '0' && code[i] <= '9') {
      int num_start = i;
      bool is_float = false;

      // consume integer part
      while (code[i] >= '0' && code[i] <= '9') {
        i++;
        column++;
      }

      // check for decimal point followed by digit
      if (code[i] == '.' && code[i + 1] >= '0' && code[i + 1] <= '9') {
        is_float = true;
        i++; // consume .
        column++;
        while (code[i] >= '0' && code[i] <= '9') {
          i++;
          column++;
        }
      }

      // check for f or F suffix (only if we have a decimal)
      if (is_float && (code[i] == 'f' || code[i] == 'F')) {
        i++;
        column++;
      }

      if (is_float) {
        int len = i - num_start;
        char *float_str = malloc(len + 1);
        strncpy(float_str, &code[num_start], len);
        float_str[len] = '\0';

        token_list_append(token_list, (Token){.type = FLOAT_LIT_T,
                                              .value.as_string = float_str,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
      } else {
        // parse as integer
        int num = 0;
        for (int j = num_start; j < i; j++) {
          num = num * 10 + (code[j] - '0');
        }
        token_list_append(token_list, (Token){.type = INT_LIT_T,
                                              .value.as_int = num,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
      }

      continue;
    }

    // string literals
    if (c == '"') {
      i++; // skip opening quote
      column++;
      int str_start = i;
      int str_len = 0;

      // find closing quote and calculate length
      while (code[i] != '"' && code[i] != '\0' && code[i] != '\n') {
        if (code[i] == '\\' && code[i + 1] != '\0') {
          i++;
          column++;
          str_len++;
        }
        i++;
        column++;
        str_len++;
      }

      if (code[i] != '"') {
        SourceLocation loc = {
            .line = line, .column = start_col, .filename = filename};
        add_error(g_error_collector, STAGE_LEXER, loc,
                  "unterminated string literal");
        continue;
      }

      char *str = malloc(str_len + 1);
      int str_i = 0;
      int j = str_start;

      while (j < i) {
        if (code[j] == '\\' && j + 1 < i) {
          j++;
          switch (code[j]) {
          case 'n':
            str[str_i++] = '\n';
            break;
          case 't':
            str[str_i++] = '\t';
            break;
          case 'r':
            str[str_i++] = '\r';
            break;
          case '\\':
            str[str_i++] = '\\';
            break;
          case '"':
            str[str_i++] = '"';
            break;
          default:
            // unknown escape sequence - just include the character
            str[str_i++] = code[j];
            break;
          }
          j++;
        } else {
          str[str_i++] = code[j++];
        }
      }
      str[str_i] = '\0';

      i++;
      column++;

      token_list_append(token_list, (Token){.type = STR_LIT_T,
                                            .value.as_string = str,
                                            .line = line,
                                            .column = start_col,
                                            .filename = filename});

      continue;
    }

    // character literals
    if (c == '\'') {
      i++; // skip opening quote
      column++;

      char char_val = 0;

      if (code[i] == '\\') {
        i++;
        column++;
        if (code[i] == 'n')
          char_val = '\n';
        else if (code[i] == 't')
          char_val = '\t';
        else if (code[i] == 'r')
          char_val = '\r';
        else if (code[i] == '0')
          char_val = '\0';
        else if (code[i] == '\\')
          char_val = '\\';
        else if (code[i] == '\'')
          char_val = '\'';
        else {
          SourceLocation loc = {
              .line = line, .column = column, .filename = filename};
          add_error(g_error_collector, STAGE_LEXER, loc,
                    "unknown escape sequence");
          char_val = code[i];
        }
        i++;
        column++;
      } else {
        char_val = code[i];
        i++;
        column++;
      }

      if (code[i] != '\'') {
        SourceLocation loc = {
            .line = line, .column = start_col, .filename = filename};
        add_error(g_error_collector, STAGE_LEXER, loc,
                  "unterminated character literal (expected ')");
        // recover by skipping until whitespace or next quote
      } else {
        i++;
        column++;
      }
      token_list_append(token_list, (Token){.type = CHAR_LIT_T,
                                            .value.as_char = char_val,
                                            .line = line,
                                            .column = start_col,
                                            .filename = filename});
      continue;
    }

    if (isalpha(code[i]) || code[i] == '_') {
      const char *start_ptr = &code[i];
      int start_col = column;
      int len = 0;

      // Consume the identifier/keyword
      while (isalnum(code[i]) || code[i] == '_') {
        i++;
        column++;
        len++;
      }
      // check for keywords using strncmp
      int keyword_index = -1;
      for (int k = 0; k < NUM_KEYWORDS; k++) {
        if (strlen(KEYWORDS[k].name) == len &&
            strncmp(start_ptr, KEYWORDS[k].name, len) == 0) {
          keyword_index = k;
          break;
        }
      }

      Token t = {.line = line, .column = start_col, .filename = filename};

      if (keyword_index != -1) {
        // keyword or boolean keyword
        t.type = KEYWORDS[keyword_index].type;
        if (t.type == BOOL_LIT_T) {
          t.value.as_bool = (start_ptr[0] == 't');
        } else {
          t.value.as_ptr = NULL;
        }
      } else {
        // variable/identifier
        t.type = VAR_T;
        // copy from code
        t.value.as_string = strndup(start_ptr, len);
      }

      token_list_append(token_list, t);
      continue;
    }
    // comments
    if (c == '/') {
      if (code[i + 1] == '/') {
        i += 2;
        column += 2;
        while (code[i] != '\n' && code[i] != '\0') {
          i++;
          column++;
        }
        continue;
      } else if (code[i + 1] == '*') {
        i += 2;
        column += 2;
        while (code[i] != '\0' && !(code[i] == '*' && code[i + 1] == '/')) {
          if (code[i] == '\n') {
            line++;
            column = 1;
          } else {
            column++;
          }
          i++;
        }
        if (code[i] != '\0') {
          i += 2;
          column += 2;
        }
        continue;
      } else {
        token_list_append(token_list, (Token){.type = SLASH_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i++;
        column++;
        continue;
      }
    }

    // two-character operators
    if (c == '=') {
      if (code[i + 1] == '=') {
        token_list_append(token_list, (Token){.type = DOUBLE_EQUALS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i += 2;
        column += 2;
      } else {
        token_list_append(token_list, (Token){.type = EQUALS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i++;
        column++;
      }
      continue;
    }

    if (c == '!') {
      if (code[i + 1] == '=') {
        token_list_append(token_list, (Token){.type = NOT_EQUALS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i += 2;
        column += 2;
      } else {
        token_list_append(token_list, (Token){.type = NEGATION_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i++;
        column++;
      }
      continue;
    }

    if (c == '<') {
      if (code[i + 1] == '=') {
        token_list_append(token_list, (Token){.type = LESS_EQUALS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i += 2;
        column += 2;
      } else {
        token_list_append(token_list, (Token){.type = LESS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i++;
        column++;
      }
      continue;
    }

    if (c == '>') {
      if (code[i + 1] == '=') {
        token_list_append(token_list, (Token){.type = MORE_EQUALS_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});

        i += 2;
        column += 2;
      } else {
        token_list_append(token_list, (Token){.type = MORE_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i++;
        column++;
      }
      continue;
    }

    if (c == '&') {
      if (code[i + 1] == '&') {
        token_list_append(token_list, (Token){.type = AND_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i += 2;
        column += 2;
      } else {
        // error with recovery - suggest && instead
        SourceLocation loc = {
            .line = line, .column = start_col, .filename = filename};
        add_error(g_error_collector, STAGE_LEXER, loc,
                  "single '&' not supported, did you mean '&&'?");
        i++;
        column++;
        continue;
      }
      continue;
    }

    if (c == '|') {
      if (code[i + 1] == '|') {
        token_list_append(token_list, (Token){.type = OR_T,
                                              .value.as_ptr = NULL,
                                              .line = line,
                                              .column = start_col,
                                              .filename = filename});
        i += 2;
        column += 2;
      } else {
        // error with recovery - suggest || instead
        SourceLocation loc = {
            .line = line, .column = start_col, .filename = filename};
        add_error(g_error_collector, STAGE_LEXER, loc,
                  "single '|' not supported, did you mean '||'?");
        i++;
        column++;
        continue;
      }
      continue;
    }

    // single-character tokens
    TokenType single_char_type;
    bool found = true;

    switch (c) {
    case '{':
      single_char_type = L_BRACE_T;
      break;
    case '}':
      single_char_type = R_BRACE_T;
      break;
    case '(':
      single_char_type = L_PAREN_T;
      break;
    case ')':
      single_char_type = R_PAREN_T;
      break;
    case '[':
      single_char_type = L_BRACKET_T;
      break;
    case ']':
      single_char_type = R_BRACKET_T;
      break;
    case '+':
      single_char_type = PLUS_T;
      break;
    case '-':
      single_char_type = MINUS_T;
      break;
    case '*':
      single_char_type = STAR_T;
      break;
    case '?':
      single_char_type = QUESTION_MARK_T;
      break;
    case '_':
      single_char_type = UNDERSCORE_T;
      break;
    case ';':
      single_char_type = SEMICOLON_T;
      break;
    case ':':
      single_char_type = COLON_T;
      break;
    case ',':
      single_char_type = COMMA_T;
      break;
    case '.':
      single_char_type = DOT_T;
      break;
    default:
      found = false;
      break;
    }

    if (found) {
      token_list_append(token_list, (Token){.type = single_char_type,
                                            .value.as_ptr = NULL,
                                            .line = line,
                                            .column = start_col,
                                            .filename = filename});
      i++;
      column++;
      continue;
    }

    // unknown character
    SourceLocation loc = {
        .line = line, .column = start_col, .filename = filename};
    add_error(g_error_collector, STAGE_LEXER, loc,
              "unexpected character '%c' (ASCII %d)", c, c);
    i++;
    column++;
  }

  token_list_append(token_list,

                    (Token){.type = EOF_T,
                            .value.as_ptr = NULL,
                            .line = line,
                            .column = column,
                            .filename = filename});
  return token_list;
}

const char *token_type_name(TokenType type) {
  switch (type) {
  case INT_LIT_T:
    return "int literal";
  case BOOL_LIT_T:
    return "bool literal";
  case STR_LIT_T:
    return "string literal";
  case NULL_LIT_T:
    return "null";
  case VAR_T:
    return "identifier";
  case PLUS_T:
    return "+";
  case MINUS_T:
    return "-";
  case STAR_T:
    return "*";
  case SLASH_T:
    return "/";
  case EQUALS_T:
    return "=";
  case DOUBLE_EQUALS_T:
    return "==";
  case NOT_EQUALS_T:
    return "!=";
  case LESS_T:
    return "<";
  case MORE_T:
    return ">";
  case LESS_EQUALS_T:
    return "<=";
  case MORE_EQUALS_T:
    return ">=";
  case NEGATION_T:
    return "!";
  case AND_T:
    return "&&";
  case OR_T:
    return "||";
  case SEMICOLON_T:
    return ";";
  case COLON_T:
    return ":";
  case L_PAREN_T:
    return "(";
  case R_PAREN_T:
    return ")";
  case L_BRACE_T:
    return "{";
  case R_BRACE_T:
    return "}";
  case UNDERSCORE_T:
    return "_";
  case IF_T:
    return "if";
  case ELSE_T:
    return "else";
  case WHILE_T:
    return "while";
  case DO_T:
    return "do";
  case FOR_T:
    return "for";
  case TO_T:
    return "to";
  case MATCH_T:
    return "match";
  case SOME_T:
    return "some";
  case INT_KEYWORD_T:
    return "int";
  case BOOL_KEYWORD_T:
    return "bool";
  case STR_KEYWORD_T:
    return "str";
  case CHAR_KEYWORD_T:
    return "char";
  case FLOAT_KEYWORD_T:
    return "float";
  case DOUBLE_KEYWORD_T:
    return "double";
  case FLOAT_LIT_T:
    return "float literal";
  case DEF_KEYWORD_T:
    return "def";
  case INCLUDE_T:
    return "include";
  case EXTERN_T:
    return "extern";
  case EOF_T:
    return "EOF";
  case COMMA_T:
    return ",";
  case DOT_T:
    return ".";
  case QUESTION_MARK_T:
    return "?";
  case RETURN_T:
    return "return";
  case VOID_KEYWORD_T:
    return "void";
  case PRINT_KEYWORD_T:
    return "print";
  case OWN_T:
    return "own";
  case REF_T:
    return "ref";
  case ALLOC_T:
    return "alloc";
  case FREE_T:
    return "free";
  case DOUBLE_SLASH_T:
    return "//";
  case COMMENT_L_T:
    return "/*";
  case COMMENT_R_T:
    return "*/";
  default:
    return "unknown";
  }
}
