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
    PERCENT_T,                          // %

    //compound assignment operators
    PLUS_EQ_T, MINUS_EQ_T, STAR_EQ_T,   // +=, -=, *=
    SLASH_EQ_T, PERCENT_EQ_T,           // /=, %=

    //increment / decrement
    PLUS_PLUS_T, MINUS_MINUS_T,         // ++, --

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

    //bitwise operators
    BIT_AND_T,          //&
    BIT_OR_T,           //|
    BIT_XOR_T,          //^
    SHL_T,              //<<
    SHR_T,              //>>

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
    USIZE_KEYWORD_T,        // unsigned size; emits as C `size_t`. Use in extern
                            // decls for any libc fn that takes a size_t (malloc,
                            // realloc, snprintf size, memcpy length) -- the
                            // plain `int` mismatch is what blocks std.list /
                            // std.string from compiling cleanly on 64-bit.
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
    STRUCT_T,
    FN_T,                  // function-pointer type prefix:  fn(T,T): R
    PTR_KEYWORD_T,         // opaque void* type

    //mem stuff

    OWN_T,
    REF_T,
    ALLOC_T,
    FREE_T,

    //other

    CONST_T,
    PRIVATE_T,
    PUBLIC_T,
    STATIC_T,
    DEFER_T,
    PIPE_T,             // |>  (function pipeline)
    UNSAFE_T,           // unsafe { ... }
    BREAK_T,
    CONTINUE_T,
    AS_T,               // for ... as <label>; break <label>; continue <label>
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
