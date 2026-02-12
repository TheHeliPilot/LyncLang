//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_LEXER_H
#define LYNC_LEXER_H

#include "common.h"

typedef enum {
    // Literals
    INT_LIT_T,
    BOOL_LIT_T,
    STR_LIT_T,
    VAR_T,

    // Arithmetic operators
    PLUS_T, MINUS_T, STAR_T, SLASH_T,

    // Comparison operators
    EQUALS_T,           // =
    DOUBLE_EQUALS_T,    // ==
    NOT_EQUALS_T,       // !=
    LESS_T,             //
    MORE_T,             // >
    LESS_EQUALS_T,      // <=
    MORE_EQUALS_T,      // >=

    // Logical operators
    NEGATION_T,         // !
    AND_T,              // &&
    OR_T,               // ||

    // Punctuation
    SEMICOLON_T,
    COLON_T,
    COMMA_T,
    DOT_T,
    QUESTION_MARK_T,

    // Braces & parentheses
    L_PAREN_T, R_PAREN_T,
    L_BRACE_T, R_BRACE_T,

    // Control flow keywords
    IF_T,
    ELSE_T,
    WHILE_T,
    DO_T,
    FOR_T,
    TO_T,
    MATCH_T,
    UNDERSCORE_T,
    RETURN_T,
    SOME_T,

    // Type keywords
    INT_KEYWORD_T,
    BOOL_KEYWORD_T,
    STR_KEYWORD_T,
    VOID_KEYWORD_T,
    NULL_LIT_T,

    // Function stuff
    DEF_KEYWORD_T,
    PRINT_KEYWORD_T,

    // Mem stuff
    OWN_T,
    REF_T,
    ALLOC_T,
    FREE_T,

    // Other
    DOUBLE_SLASH_T,
    COMMENT_L_T,
    COMMENT_R_T,
    EOF_T,
} TokenType;

typedef struct {
    TokenType type;
    void* value;
    int line;
    int column;
    const char* filename;
} Token;

Token* tokenize(char* code, int* out_count, const char* filename);
void print_tokens(Token* tokens, int count);
const char* token_type_name(TokenType);


#endif //LYNC_LEXER_H
