//created by bucka on 2/9/2026.

#ifndef LYNC_LEXER_H
#define LYNC_LEXER_H

#include "common.h"

typedef enum {
    //literals
    INT_LIT_T,
    BOOL_LIT_T,
    STR_LIT_T,
    CHAR_LIT_T,
    VAR_T,
    FLOAT_LIT_T,
    NULL_LIT_T,

    //arithmetic operators
    PLUS_T, MINUS_T, STAR_T, SLASH_T,

    //comparison operators
    EQUALS_T,           //=
    DOUBLE_EQUALS_T,    //==
    NOT_EQUALS_T,       //!=
    LESS_T,
    MORE_T,             //>
    LESS_EQUALS_T,      //<=
    MORE_EQUALS_T,      //>=

    //logical operators
    NEGATION_T,         //!
    AND_T,              //&&
    OR_T,               //||

    //punctuation

    SEMICOLON_T,
    COLON_T,
    COMMA_T,
    DOT_T,
    QUESTION_MARK_T,

    //braces & parentheses
    L_PAREN_T, R_PAREN_T,
    L_BRACE_T, R_BRACE_T,
    L_BRACKET_T, R_BRACKET_T,

    //control flow keywords

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

    //type keywords
    INT_KEYWORD_T,
    BOOL_KEYWORD_T,
    STR_KEYWORD_T,
    CHAR_KEYWORD_T,
    FLOAT_KEYWORD_T,
    DOUBLE_KEYWORD_T,
    VOID_KEYWORD_T,

    //function stuff

    DEF_KEYWORD_T,
    PRINT_KEYWORD_T,
    INCLUDE_T,
    EXTERN_T,

    //mem stuff

    OWN_T,
    REF_T,
    ALLOC_T,
    FREE_T,

    //other

    CONST_T,
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


#endif //lYNC_LEXER_H
