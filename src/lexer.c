// created by bucka on 2/9/2026.

#include "lexer.h"
#include "error.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEX_LOC(lex)                                                           \
  ((SourceLocation){.line = (lex)->line,                                       \
                    .column = (lex)->column,                                   \
                    .filename = (lex)->filename})

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
  return TOKEN_APPEND_SUCCESS;
}

Token *token_list_at(TokenList *l, size_t i) {
  assert(i < l->count && "Token Index out of bounds");
  return &(l->tokens[i]);
}

void print_token(Token *t) {
  if (!g_trace_mode)
    return;
  fprintf(stdout, "%-15s", token_type_name(t->type));

  switch (t->type) {
  case INT_LIT_T:
    fprintf(stdout, " = %lld", t->value.as_int);
    break;
  case FLOAT_LIT_T:
    fprintf(stdout, " = %f", t->value.as_float);
    break;
  case CHAR_LIT_T:
    fprintf(stdout, " = '%c'", t->value.as_char);
    break;
  case BOOL_LIT_T:
    fprintf(stdout, " = %s", t->value.as_bool ? "true" : "false");
    break;
  case STR_LIT_T:
  case VAR_T:
    if (t->value.as_string) {
      fprintf(stdout, " = \"%s\"", t->value.as_string);
    }
    break;
  case NULL_LIT_T:
    fprintf(stdout, " = null");
    break;
  default:
    break;
  }
}

void token_list_print(TokenList *l) {
  if (!g_trace_mode)
    return;

  fprintf(stdout, "=== TOKENS (%zu) ===\n", l->count);

  for (size_t i = 0; i < l->count; i++) {
    Token *t = token_list_at(l, i);

    fprintf(stdout, "[%3zu] [%s:%ld:%ld] ", i, t->filename, t->line, t->column);
    print_token(t);
    fprintf(stdout, "\n");
  }
  fprintf(stdout, "==================\n");
}
void token_list_free(TokenList *l) {
  free(l->tokens);
  free(l);
}

typedef enum {
  LEX_START,
  LEX_INTEGER,
  LEX_FLOAT,
  LEX_IDENTIFIER,
  LEX_STRING,
  LEX_STRING_ESCAPE,
  LEX_CHAR,
  LEX_CHAR_ESCAPE,
  LEX_SAW_SLASH,
  LEX_COMMENT_LINE,
  LEX_COMMENT_BLOCK,
  LEX_SAW_EQUALS,
  LEX_SAW_BANG,
  LEX_SAW_LESS,
  LEX_SAW_MORE,
  LEX_SAW_AMPERSAND,
  LEX_SAW_PIPE,
  LEX_DONE,
  LEX_ERROR
} LexerState;

typedef struct {
  LexerState state;     // current state
  size_t cursor;        // curent index into code
  size_t line;          // current line number in code
  size_t column;        // current column in line
  const char *code;     // code to tokenie
  const char *filename; // name of file
} Lexer;

#define LEXER_NULL_TERMINATOR '\0'

static char lexer_peek(Lexer *lexer) {
  if (lexer->code[lexer->cursor] == LEXER_NULL_TERMINATOR)
    return LEXER_NULL_TERMINATOR;
  return lexer->code[lexer->cursor];
}

static char lexer_peek_next(Lexer *lexer) {
  if (lexer->code[lexer->cursor] == LEXER_NULL_TERMINATOR)
    return LEXER_NULL_TERMINATOR;
  return lexer->code[lexer->cursor + 1];
}
static char lexer_advance(Lexer *lexer) {
  char current = lexer_peek(lexer);
  if (current == LEXER_NULL_TERMINATOR) {
    return LEXER_NULL_TERMINATOR;
  }
  lexer->cursor++;
  if (current == '\n') {
    lexer->line++;
    lexer->column = 1;
  } else {
    lexer->column++;
  }
  return current;
}

static TokenType get_punctuation_type(char c) {
  switch (c) {
  case '{':
    return L_BRACE_T;
  case '}':
    return R_BRACE_T;
  case '(':
    return L_PAREN_T;
  case ')':
    return R_PAREN_T;
  case '[':
    return L_BRACKET_T;
  case ']':
    return R_BRACKET_T;
  case '+':
    return PLUS_T;
  case '-':
    return MINUS_T;
  case '*':
    return STAR_T;
  case '?':
    return QUESTION_MARK_T;
  case ';':
    return SEMICOLON_T;
  case ':':
    return COLON_T;
  case ',':
    return COMMA_T;
  case '.':
    return DOT_T;
  default:
    return 0;
  }
}

static Token make_token(TokenType type, Lexer *lexer, size_t start_col) {
  Token t;
  t.type = type;
  t.line = lexer->line;
  t.column = start_col;
  t.filename = lexer->filename;
  memset(&t.value, 0, sizeof(t.value));
  t.value.as_null = NULL;
  return t;
}

static Token make_int_token(Lexer *lexer, size_t start_index,
                            size_t start_col) {
  Token t = make_token(INT_LIT_T, lexer, start_col);

  long long num = 0;
  for (size_t i = start_index; i < lexer->cursor; i++) {
    num = num * 10 + (lexer->code[i] - '0');
  }
  t.value.as_int = num;
  return t;
}
static Token make_float_token(Lexer *lexer, size_t start_index,
                              size_t start_col) {
  Token t = make_token(FLOAT_LIT_T, lexer, start_col);
  size_t len = lexer->cursor - start_index;
  char *str = malloc(len + 1);
  if (str) {
    strncpy(str, lexer->code + start_index, len);
    str[len] = '\0';
    t.value.as_float = strtod(str, NULL);
    free(str);
  }
  return t;
}

static Token make_string_token(Lexer *lexer, size_t start_index,
                               size_t start_col) {
  Token t = make_token(STR_LIT_T, lexer, start_col);
  // we get the lenght of the actual string literal removing the start and end
  // quotes
  size_t raw_str_len = (lexer->cursor - 1) - (start_index + 1);
  char *raw_str = (char *)lexer->code + start_index + 1;

  // str value will be <= to raw_str_len. Add 1 for null terminator
  char *str_value = malloc(raw_str_len + 1);
  size_t str_value_idx = 0;
  for (size_t i = 0; i < raw_str_len; i++) {
    // handles escape sequence
    if (raw_str[i] == '\\' && i + 1 < raw_str_len) {
      i++; // skip escape char
      switch (raw_str[i]) {
      case 'n':
        str_value[str_value_idx++] = '\n';
        break;
      case 't':
        str_value[str_value_idx++] = '\t';
        break;
      case 'r':
        str_value[str_value_idx++] = '\r';
        break;
      case '\\':
        str_value[str_value_idx++] = '\\';
        break;
      case '"':
        str_value[str_value_idx++] = '"';
        break;
      case '0':
        str_value[str_value_idx++] = '\0';
        break;
      default:
        str_value[str_value_idx++] = raw_str[i];
        break;
      }
    } else {
      str_value[str_value_idx++] = raw_str[i];
    }
  }
  // set null terminator
  str_value[str_value_idx] = '\0';
  t.value.as_string = str_value;
  return t;
}
static Token make_char_token(Lexer *lexer, size_t start_index,
                             size_t start_col) {
  Token t = make_token(CHAR_LIT_T, lexer, start_col);
  size_t char_len = (lexer->cursor - 1) - (start_index + 1);
  char *content = (char *)lexer->code + start_index + 1;
  if (char_len == 1) {
    t.value.as_char = content[0];
    return t;
  }
  assert((char_len == 2 && content[0] == '\\') && "expected char literal");
  char val = '\0';
  switch (content[1]) {
  case 'n':
    val = '\n';
    break;
  case 't':
    val = '\t';
    break;
  case 'r':
    val = '\r';
    break;
  case '0':
    val = '\0';
    break;
  case '\\':
    val = '\\';
    break;
  case '\'':
    val = '\'';
    break;
  case '"':
    val = '"';
    break;
  default:
    val = content[1];
    break;
  }
  t.value.as_char = val;
  return t;
}

static Token make_identifier_token(Lexer *lexer, size_t start_index,
                                   size_t start_column) {
  // start of identifier
  const char *start_ptr = &lexer->code[start_index];
  size_t len = lexer->cursor - start_index;
  int keyword_index = -1;

  // check for keywords using strncmp
  for (int k = 0; k < NUM_KEYWORDS; k++) {
    if (strlen(KEYWORDS[k].name) == len &&
        strncmp(start_ptr, KEYWORDS[k].name, len) == 0) {
      keyword_index = k;
      break;
    }
  }

  if (keyword_index != -1) {
    // we found matching keyword
    TokenType tokenType = KEYWORDS[keyword_index].type;
    Token t = make_token(tokenType, lexer, start_column);
    switch (t.type) {
    case BOOL_LIT_T:
      t.value.as_bool = (start_ptr[0] == 't');
      break;
    default:
      t.value.as_null = NULL;
      break;
    }
    return t;
  }
  Token t = make_token(VAR_T, lexer, start_column);
  t.value.as_string = strndup(start_ptr, len);
  return t;
}

// implements Finite state machine for lexer state
Token next_token(Lexer *lexer) {
  LexerState state = LEX_START;
  size_t start_index = lexer->cursor;
  size_t start_col = lexer->column;
  while (true) {
    char c = lexer_peek(lexer);
    switch (state) {

    case LEX_ERROR:
      state = LEX_START;
      break;
    case LEX_DONE:
      return make_token(EOF_T, lexer, start_col);
      break;
    case LEX_START:
      if (c == LEXER_NULL_TERMINATOR) {
        state = LEX_DONE;
        lexer_advance(lexer);
      } else if (isspace((unsigned char)c)) {
        lexer_advance(lexer);
        start_index = lexer->cursor;
        start_col = lexer->column;
      } else if (isdigit(c)) {
        state = LEX_INTEGER;
        lexer_advance(lexer);
      } else if (isalpha(c) || c == '_') {
        state = LEX_IDENTIFIER;
        lexer_advance(lexer);
      } else if (c == '"') {
        state = LEX_STRING;
        lexer_advance(lexer);
      } else if (c == '\'') {
        state = LEX_CHAR;
        lexer_advance(lexer);
      } else if (c == '/') {
        state = LEX_SAW_SLASH;
        lexer_advance(lexer);
      } else if (c == '=') {
        state = LEX_SAW_EQUALS;
        lexer_advance(lexer);
      } else if (c == '!') {
        state = LEX_SAW_BANG;
        lexer_advance(lexer);
      } else if (c == '<') {
        state = LEX_SAW_LESS;
        lexer_advance(lexer);
      } else if (c == '>') {
        state = LEX_SAW_MORE;
        lexer_advance(lexer);
      } else if (c == '&') {
        state = LEX_SAW_AMPERSAND;
        lexer_advance(lexer);
      } else if (c == '|') {
        state = LEX_SAW_PIPE;
        lexer_advance(lexer);
      } else {
        lexer_advance(lexer);
        return make_token(get_punctuation_type(c), lexer, start_col);
      }
      break;
    case LEX_INTEGER:
      if (isdigit(c)) {
        lexer_advance(lexer);
      } else if (c == '.') {
        char next = lexer_peek_next(lexer);
        // we have some number like ddd.d
        if (isdigit(next)) {
          state = LEX_FLOAT;
          lexer_advance(lexer);
        } else {
          return make_int_token(lexer, start_index, start_col);
        }
      } else {
        return make_int_token(lexer, start_index, start_col);
      }
      break;
    case LEX_FLOAT:
      if (isdigit(c)) {
        lexer_advance(lexer);
      } else {
        return make_float_token(lexer, start_index, start_col);
      }
      break;
    case LEX_STRING:
      if (c == '"') {
        lexer_advance(lexer);
        return make_string_token(lexer, start_index, start_col);
      } else if (c == '\\') {
        state = LEX_STRING_ESCAPE;
        lexer_advance(lexer);
      } else if (c == LEXER_NULL_TERMINATOR || c == '\n') {
        // string didnt end before before newline
        add_error(g_error_collector, STAGE_LEXER, LEX_LOC(lexer),
                  "unterminated string literal");
        state = LEX_ERROR;
      } else {
        lexer_advance(lexer);
      }
      break;
    case LEX_STRING_ESCAPE:
      // we are escaping the next char so we dont care what it is
      lexer_advance(lexer);
      state = LEX_STRING;
      break;
    case LEX_CHAR:
      if (c == '\'') {
        lexer_advance(lexer);
        return make_char_token(lexer, start_index, start_col);
      } else if (c == '\\') {
        state = LEX_CHAR_ESCAPE;
        lexer_advance(lexer);
      } else if (c == LEXER_NULL_TERMINATOR || c == '\n') {
        // char literal didnt end
        add_error(g_error_collector, STAGE_LEXER, LEX_LOC(lexer),
                  "unterminated character literal (expected \''\')");
        state = LEX_ERROR;
      } else {
        lexer_advance(lexer);
      }
      break;
    case LEX_CHAR_ESCAPE:
      // we are escaping the next char so we dont care what it is
      lexer_advance(lexer);
      state = LEX_CHAR;
      break;
    case LEX_IDENTIFIER:
      if (isalnum(c) || c == '_') {
        lexer_advance(lexer);
      } else {
        // we found end of identifier
        return make_identifier_token(lexer, start_index, start_col);
      }
      break;
    // check for comment
    case LEX_SAW_SLASH:
      if (c == '/') {
        state = LEX_COMMENT_LINE;
      } else if (c == '*') {
        state = LEX_COMMENT_BLOCK;
      } else {
        // division symbol
        lexer_advance(lexer);
        return make_token(SLASH_T, lexer, start_col);
      }
      lexer_advance(lexer);
      break;

    case LEX_COMMENT_LINE:
      if (c == LEXER_NULL_TERMINATOR || c == '\n' || c == '\r') {
        state = LEX_START;
        // we need to advance lexer and then reset vars
        lexer_advance(lexer);
        start_index = lexer->cursor;
        start_col = lexer->column;
        break;
      }
      lexer_advance(lexer);
      break;
    // inside comment block /*
    case LEX_COMMENT_BLOCK:
      if (c == '*') {
        char next = lexer_peek_next(lexer);
        // we encounter a star which might be start of end block */
        if (next == '/') {
          state = LEX_START;
          lexer_advance(lexer);
          start_index = lexer->cursor;
          start_col = lexer->column;
        }
      }
      if (c == LEXER_NULL_TERMINATOR) {
        // end of file  before end of comment
        add_error(g_error_collector, STAGE_LEXER, LEX_LOC(lexer),
                  "unterminated comment block");
        state = LEX_ERROR;
      }
      lexer_advance(lexer);
      break;
    case LEX_SAW_EQUALS:
      if (c == '=') {
        lexer_advance(lexer);
        return make_token(DOUBLE_EQUALS_T, lexer, start_col);
      }
      return make_token(EQUALS_T, lexer, start_col);
      break;
    case LEX_SAW_BANG:
      if (c == '=') {
        lexer_advance(lexer);
        return make_token(NOT_EQUALS_T, lexer, start_col);
      }
      return make_token(NEGATION_T, lexer, start_col);
      break;
    case LEX_SAW_LESS:
      if (c == '=') {
        lexer_advance(lexer);
        return make_token(LESS_EQUALS_T, lexer, start_col);
      }
      return make_token(LESS_T, lexer, start_col);
      break;
    case LEX_SAW_MORE:
      if (c == '=') {
        lexer_advance(lexer);
        return make_token(MORE_EQUALS_T, lexer, start_col);
      }
      return make_token(MORE_T, lexer, start_col);
      break;
    case LEX_SAW_AMPERSAND:
      if (c == '&') {
        lexer_advance(lexer);
        return make_token(AND_T, lexer, start_col);
      } else {
        add_error(g_error_collector, STAGE_LEXER, LEX_LOC(lexer),
                  "single '&' not supported, did you mean '&&'?");
        state = LEX_ERROR;
      }
      break;
    case LEX_SAW_PIPE:
      if (c == '|') {
        lexer_advance(lexer);
        return make_token(OR_T, lexer, start_col);
      } else {
        add_error(g_error_collector, STAGE_LEXER, LEX_LOC(lexer),
                  "single '|' not supported, did you mean '||'?");
        state = LEX_ERROR;
      }
      break;
    }
  }
}
TokenList *tokenize(char *code, const char *filename) {
  Lexer lexer = {.state = LEX_START,
                 .cursor = 0,
                 .line = 1,
                 .column = 1,
                 .code = code,
                 .filename = filename};
  TokenList *token_list = token_list_create();
  while (lexer.state != LEX_DONE && lexer.state != LEX_ERROR) {
    Token t = next_token(&lexer);
    token_list_append(token_list, t);
    if (t.type == EOF_T) {
      break;
    }
  }
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
