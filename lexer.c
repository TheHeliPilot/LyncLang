//
// Created by bucka on 2/9/2026.
//

#include "lexer.h"
#include "error.h"

extern ErrorCollector* g_error_collector;
extern bool g_trace_mode;

Token* tokenize(char* code, int* out_count) {
    int capacity = 20;
    int count = 0;
    Token* tokens = malloc(capacity * sizeof(Token));

    // Position tracking
    int line = 1;
    int column = 1;
    const char* filename = "test.lync";  // TODO: pass as parameter

    int i = 0;
    while (code[i] != '\0') {
        char c = code[i];
        int start_col = column;  // Save column at start of token

        // Track newlines for line/column
        if (c == '\n') {
            line++;
            column = 1;
            i++;
            continue;
        }

        // Skip whitespace
        if (c == ' ' || c == '\t' || c == '\r') {
            column++;
            i++;
            continue;
        }

        // Number literals
        if (code[i] >= '0' && code[i] <= '9') {
            int num = 0;
            while (code[i] >= '0' && code[i] <= '9') {
                num = num * 10 + (code[i] - '0');
                i++;
                column++;
            }

            int* heap_num = malloc(sizeof(int));
            *heap_num = num;
            tokens[count++] = (Token){
                .type = INT_LIT_T,
                .value = heap_num,
                .line = line,
                .column = start_col,
                .filename = filename
            };

            if (count >= capacity) {
                capacity *= 2;
                tokens = realloc(tokens, capacity * sizeof(Token));
            }
            continue;
        }

        // Keywords and identifiers
        if ((code[i] >= 'a' && code[i] <= 'z') || (code[i] >= 'A' && code[i] <= 'Z')) {
            int start = i;
            while ((code[i] >= 'a' && code[i] <= 'z') ||
                   (code[i] >= 'A' && code[i] <= 'Z') ||
                   (code[i] >= '0' && code[i] <= '9')) {
                i++;
                column++;
            }

            int len = i - start;
            char* word = malloc(len + 1);
            strncpy(word, &code[start], len);
            word[len] = '\0';

            TokenType type = VAR_T;
            void* value = word;
            bool free_word = false;

            // Check keywords
            if (strcmp(word, "if") == 0) { type = IF_T; value = NULL; free_word = true; }
            else if (strcmp(word, "else") == 0) { type = ELSE_T; value = NULL; free_word = true; }
            else if (strcmp(word, "int") == 0) { type = INT_KEYWORD_T; value = NULL; free_word = true; }
            else if (strcmp(word, "void") == 0) { type = VOID_KEYWORD_T; value = NULL; free_word = true; }
            else if (strcmp(word, "bool") == 0) { type = BOOL_KEYWORD_T; value = NULL; free_word = true; }
            else if (strcmp(word, "def") == 0) { type = DEF_KEYWORD_T; value = NULL; free_word = true; }
            else if (strcmp(word, "while") == 0) { type = WHILE_T; value = NULL; free_word = true; }
            else if (strcmp(word, "do") == 0) { type = DO_T; value = NULL; free_word = true; }
            else if (strcmp(word, "for") == 0) { type = FOR_T; value = NULL; free_word = true; }
            else if (strcmp(word, "to") == 0) { type = TO_T; value = NULL; free_word = true; }
            else if (strcmp(word, "return") == 0) { type = RETURN_T; value = NULL; free_word = true; }
            else if (strcmp(word, "alloc") == 0) { type = ALLOC_T; value = NULL; free_word = true; }
            else if (strcmp(word, "free") == 0) { type = FREE_T; value = NULL; free_word = true; }
            else if (strcmp(word, "match") == 0) { type = MATCH_T; value = NULL; free_word = true; }
            else if (strcmp(word, "own") == 0) { type = OWN_T; value = NULL; free_word = true; }
            else if (strcmp(word, "ref") == 0) { type = REF_T; value = NULL; free_word = true; }
            else if (strcmp(word, "print") == 0) { type = PRINT_KEYWORD_T; value = NULL; free_word = true; }
            else if (strcmp(word, "true") == 0) {
                type = BOOL_LIT_T;
                int* val = malloc(sizeof(int));
                *val = 1;
                value = val;
                free_word = true;
            }
            else if (strcmp(word, "false") == 0) {
                type = BOOL_LIT_T;
                int* val = malloc(sizeof(int));
                *val = 0;
                value = val;
                free_word = true;
            }

            tokens[count++] = (Token){
                .type = type,
                .value = value,
                .line = line,
                .column = start_col,
                .filename = filename
            };

            if (free_word) free(word);

            if (count >= capacity) {
                capacity *= 2;
                tokens = realloc(tokens, capacity * sizeof(Token));
            }
            continue;
        }

        // Comments
        if (c == '/') {
            if (code[i + 1] == '/') {
                // Single-line comment
                i += 2;
                column += 2;
                while (code[i] != '\n' && code[i] != '\0') {
                    i++;
                    column++;
                }
                continue;
            }
            else if (code[i + 1] == '*') {
                // Multi-line comment
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
            }
            else {
                tokens[count++] = (Token){
                    .type = SLASH_T,
                    .value = NULL,
                    .line = line,
                    .column = start_col,
                    .filename = filename
                };
                i++;
                column++;
                continue;
            }
        }

        // Two-character operators
        if (c == '=') {
            if (code[i + 1] == '=') {
                tokens[count++] = (Token){ .type = DOUBLE_EQUALS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
            } else {
                tokens[count++] = (Token){ .type = EQUALS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i++;
                column++;
            }
            goto resize_check;
        }

        if (c == '!') {
            if (code[i + 1] == '=') {
                tokens[count++] = (Token){ .type = NOT_EQUALS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
            } else {
                tokens[count++] = (Token){ .type = NEGATION_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i++;
                column++;
            }
            goto resize_check;
        }

        if (c == '<') {
            if (code[i + 1] == '=') {
                tokens[count++] = (Token){ .type = LESS_EQUALS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
            } else {
                tokens[count++] = (Token){ .type = LESS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i++;
                column++;
            }
            goto resize_check;
        }

        if (c == '>') {
            if (code[i + 1] == '=') {
                tokens[count++] = (Token){ .type = MORE_EQUALS_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
            } else {
                tokens[count++] = (Token){ .type = MORE_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i++;
                column++;
            }
            goto resize_check;
        }

        if (c == '&') {
            if (code[i + 1] == '&') {
                tokens[count++] = (Token){ .type = AND_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
                goto resize_check;
            } else {
                // Error with recovery - suggest && instead
                SourceLocation loc = {.line = line, .column = start_col, .filename = filename};
                add_error(g_error_collector, STAGE_LEXER, loc, "single '&' not supported, did you mean '&&'?");
                i++;
                column++;
                continue;
            }
        }

        if (c == '|') {
            if (code[i + 1] == '|') {
                tokens[count++] = (Token){ .type = OR_T, .value = NULL, .line = line, .column = start_col, .filename = filename };
                i += 2;
                column += 2;
                goto resize_check;
            } else {
                // Error with recovery - suggest || instead
                SourceLocation loc = {.line = line, .column = start_col, .filename = filename};
                add_error(g_error_collector, STAGE_LEXER, loc, "single '|' not supported, did you mean '||'?");
                i++;
                column++;
                continue;
            }
        }

        // Single-character tokens
        TokenType single_char_type;
        bool found = true;

        switch (c) {
            case '{': single_char_type = L_BRACE_T; break;
            case '}': single_char_type = R_BRACE_T; break;
            case '(': single_char_type = L_PAREN_T; break;
            case ')': single_char_type = R_PAREN_T; break;
            case '+': single_char_type = PLUS_T; break;
            case '-': single_char_type = MINUS_T; break;
            case '*': single_char_type = STAR_T; break;
            case '?': single_char_type = QUESTION_MARK_T; break;
            case '_': single_char_type = UNDERSCORE_T; break;
            case ';': single_char_type = SEMICOLON_T; break;
            case ':': single_char_type = COLON_T; break;
            case ',': single_char_type = COMMA_T; break;
            default: found = false; break;
        }

        if (found) {
            tokens[count++] = (Token){
                .type = single_char_type,
                .value = NULL,
                .line = line,
                .column = start_col,
                .filename = filename
            };
            i++;
            column++;
            goto resize_check;
        }

        // Unknown character - error with recovery
        SourceLocation loc = {.line = line, .column = start_col, .filename = filename};
        add_error(g_error_collector, STAGE_LEXER, loc,
                  "unexpected character '%c' (ASCII %d)", c, c);
        i++;
        column++;
        continue;

resize_check:
        if (count >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, capacity * sizeof(Token));
        }
    }

    // Add EOF token
    if (count >= capacity) {
        capacity *= 2;
        tokens = realloc(tokens, capacity * sizeof(Token));
    }
    tokens[count++] = (Token){
        .type = EOF_T,
        .value = NULL,
        .line = line,
        .column = column,
        .filename = filename
    };

    *out_count = count;
    return tokens;
}

void print_tokens(Token* tokens, int count) {
    if (!g_trace_mode) return;
    fprintf(stderr, "=== TOKENS (%d) ===\n", count);
    for (int i = 0; i < count; i++) {
        fprintf(stderr, "[%3d] [%s:%d:%d] ", i, tokens[i].filename, tokens[i].line, tokens[i].column);
        fprintf(stderr, "%s", token_type_name(tokens[i].type));

        if (tokens[i].value != NULL) {
            if (tokens[i].type == INT_LIT_T || tokens[i].type == BOOL_LIT_T) {
                fprintf(stderr, " = %d", *(int*)tokens[i].value);
            } else if (tokens[i].type == VAR_T) {
                fprintf(stderr, " = \"%s\"", (char*)tokens[i].value);
            }
        }

        fprintf(stderr, "\n");
    }
    fprintf(stderr, "==================\n");
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case INT_LIT_T: return "int literal";
        case BOOL_LIT_T: return "bool literal";
        case VAR_T: return "identifier";
        case PLUS_T: return "+";
        case MINUS_T: return "-";
        case STAR_T: return "*";
        case SLASH_T: return "/";
        case EQUALS_T: return "=";
        case DOUBLE_EQUALS_T: return "==";
        case NOT_EQUALS_T: return "!=";
        case LESS_T: return "<";
        case MORE_T: return ">";
        case LESS_EQUALS_T: return "<=";
        case MORE_EQUALS_T: return ">=";
        case NEGATION_T: return "!";
        case AND_T: return "&&";
        case OR_T: return "||";
        case SEMICOLON_T: return ";";
        case COLON_T: return ":";
        case L_PAREN_T: return "(";
        case R_PAREN_T: return ")";
        case L_BRACE_T: return "{";
        case R_BRACE_T: return "}";
        case UNDERSCORE_T: return "_";
        case IF_T: return "if";
        case ELSE_T: return "else";
        case WHILE_T: return "while";
        case DO_T: return "do";
        case FOR_T: return "for";
        case TO_T: return "to";
        case MATCH_T: return "match";
        case INT_KEYWORD_T: return "int";
        case BOOL_KEYWORD_T: return "bool";
        case DEF_KEYWORD_T: return "def";
        case EOF_T: return "EOF";
        case COMMA_T: return ",";
        case DOT_T: return ".";
        case QUESTION_MARK_T: return "?";
        case RETURN_T: return "return";
        case VOID_KEYWORD_T: return "void";
        case PRINT_KEYWORD_T: return "print";
        case OWN_T: return "own";
        case REF_T: return "ref";
        case ALLOC_T: return "alloc";
        case FREE_T: return "free";
        case DOUBLE_SLASH_T: return "//";
        case COMMENT_L_T: return "/*";
        case COMMENT_R_T: return "*/";
        default: return "unknown";
    }
}
