//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_PARSER_H
#define LYNC_PARSER_H

#include "common.h"
#include "lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Func Func;

typedef enum {
    OWNERSHIP_NONE,
    OWNERSHIP_OWN,
    OWNERSHIP_REF,
} Ownership;

typedef struct {
    TokenType type;
    char* name;
    Ownership ownership;
    bool isNullable;
} FuncParam;

typedef struct {
    char* name;
    FuncParam* parameters;
    int paramNum;
    TokenType retType;
} FuncSign;

struct Func {
    FuncSign* signature;
    Stmt* body;
};

typedef enum {
    NULL_PATTERN,      // null
    SOME_PATTERN,      // some(binding_name)
    WILDCARD_PATTERN,  // _
    VALUE_PATTERN,     // any expression (for non-nullable matches)
} PatternType;

typedef struct Pattern {
    PatternType type;
    SourceLocation loc;
    union {
        char* binding_name;  // for SOME_PATTERN
        Expr* value_expr;    // for VALUE_PATTERN
    } as;
} Pattern;

typedef struct {
    Pattern* pattern;       // CHANGED: was Expr* caseExpr
    Expr* caseRet;
    TokenType analyzed_type; // for codegen (filled by analyzer)
} MatchBranchExpr;

typedef struct {
    Pattern* pattern;           // CHANGED: was Expr* caseExpr
    Stmt** stmts;
    int stmtCount;
    TokenType analyzed_type;    // for codegen (filled by analyzer)
} MatchBranchStmt;

typedef enum {
    //literals
    INT_LIT_E, BOOL_LIT_E, STR_LIT_E, NULL_LIT_E,

    //vars
    VAR_E,

    //funcs
    FUNC_CALL_E, FUNC_RET_E,

    //other
    MATCH_E, VOID_E,

    //mem
    ALLOC_E,

    //match
    SOME_E,

    //operations
    UN_OP_E, BIN_OP_E,

} ExprType;

struct Expr {
    ExprType type;
    SourceLocation loc;
    TokenType analyzedType;  //filled in by analyzer

    union {
        int int_val;

        int bool_val;

        char* str_val;

        struct {
            char* name;
            Ownership ownership;
        } var;

        struct {
            TokenType op;
            struct Expr* expr;
        } un_op;

        struct {
            struct Expr* exprL;
            TokenType op;
            struct Expr* exprR;
        } bin_op;

        struct {
            char* name;
            Expr** params;
            int count;
            FuncSign* resolved_sign;
        } func_call;

        Expr* func_ret_expr;

        struct {
            Expr* initialValue;
            TokenType type;
        } alloc;

        struct {
            Expr* var;
            MatchBranchExpr* branches;
            int branchCount;
        } match;

        struct {
            Expr* var;
        } some;
    } as;
};

typedef enum {
    VAR_DECL_S,         // x: int = 5;
    ASSIGN_S,           // x = 5;
    IF_S,               // if cond { } else { }
    WHILE_S,            // while cond { }
    DO_WHILE_S,         // do { } while cond
    FOR_S,              // for (var: min to max) { }
    BLOCK_S,            // { stmt; stmt; stmt; }
    MATCH_S,
    FREE_S,
    EXPR_STMT_S,        // expression as statement (for function calls later)
} StmtType;

struct Stmt {
    StmtType type;
    SourceLocation loc;

    union {
        struct {
            char* name;
            TokenType varType;
            Expr* expr;
            Ownership ownership;
            bool isNullable;
        } var_decl;

        struct {
            char* name;
            Expr* expr;
            Ownership ownership;
        } var_assign;

        struct {
            Expr* cond;
            Stmt* trueStmt;
            Stmt* falseStmt;
        } if_stmt;

        struct {
            Expr* cond;
            Stmt* body;
        } while_stmt;

        struct {
            Expr* cond;
            Stmt* body;
        } do_while_stmt;

        struct {
            char* varName;
            Expr* min;
            Expr* max;
            Stmt* body;
        } for_stmt;

        struct {
            Stmt** stmts;
            int count;
        } block_stmt;

        struct {
            Expr* var;
            MatchBranchStmt* branches;
            int branchCount;
        } match_stmt;

        struct {
            char* varName;
        } free_stmt;

        Expr* expr_stmt;

    } as;
};

typedef struct {
    Token* tokens;
    int count;
    int size;
    int pos;
} Parser;

Token* peek(Parser*, int);
Token* consume(Parser*);
Token* expect(Parser*, TokenType);

// Expr constructors
Expr* makeIntLit(SourceLocation, int);
Expr* makeBoolLit(SourceLocation, bool);
Expr* makeStrLit(SourceLocation, char*);
Expr* makeNullLit(SourceLocation);
Expr* makeVar(SourceLocation, char*);
Expr* makeFuncCall(SourceLocation, char*, Expr**, int);
Expr* makeUnOp(SourceLocation, TokenType, Expr*);
Expr* makeBinOp(SourceLocation, Expr*, TokenType, Expr*);

// Statement constructors
Stmt* makeVarDecl(SourceLocation, char*, TokenType, Expr*);
Stmt* makeAssign(SourceLocation, char*, Expr*);
Stmt* makeIf(SourceLocation, Expr*, Stmt*, Stmt*);
Stmt* makeWhile(SourceLocation, Expr*, Stmt*);
Stmt* makeBlock(SourceLocation, Stmt**, int);
Stmt* makeExprStmt(SourceLocation, Expr*);

Func* makeFunc(char*, FuncParam*, int, TokenType, Stmt*);
bool check_func_sign(FuncSign *a, FuncSign *b);
bool check_func_sign_unwrapped(FuncSign* a, char* name, int paramNum, Expr** parameters);

// Parsing
Func** parseProgram(Parser*, int*);
Stmt* parseStatement(Parser*);
Stmt* parseBlock(Parser*);

Expr* parseExpr(Parser*);
Expr* parseAnd(Parser*);
Expr* parseComparison(Parser*);
Expr* parseAdd(Parser*);
Expr* parseTerm(Parser*);
Expr* parseFactor(Parser*);
Func** parseFunctions(Parser* p, int*);
FuncParam* parseFuncParams(Parser*, int*);

void print_ast(Func**, int);

#endif //LYNC_PARSER_H
