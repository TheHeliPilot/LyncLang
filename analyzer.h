//
// Created by bucka on 2/9/2026.
//

#ifndef CMINUSMINUS_ANALYZER_H
#define CMINUSMINUS_ANALYZER_H

#include "common.h"
#include "parser.h"

typedef enum {
    ALIVE,   // can be used
    MOVED,   // ownership transferred, cannot be used
    FREED,   // has been freed, cannot be used
} VarState;

typedef struct {
    TokenType type;
    char* name;
    Ownership ownership;
    VarState state;
    bool is_nullable;
} Symbol;

typedef struct Scope Scope;
struct Scope {
    Symbol* symbols;
    int count;
    int capacity;

    Scope* parent;
};

typedef struct FuncTable FuncTable;
struct FuncTable {
    FuncSign* signs;
    int count;
    int capacity;
};

void defineAndAnalyzeFunc(FuncTable* table, Func* func);
FuncSign* lookup_func_sign(FuncTable *t, FuncSign *s);
FuncSign* lookup_func_name(FuncTable *t, char *s);

Scope* make_scope(Scope* parent);
FuncTable* make_funcTable();

void declare(Scope*, char* name, TokenType type, Ownership ownership);
Symbol* lookup(Scope*, char* name);

TokenType analyze_expr(Scope*, FuncTable*, Expr*);
void analyze_stmt(Scope*, FuncTable*, Stmt*);
void analyze_program(Func**, int);

#endif //CMINUSMINUS_ANALYZER_H
