//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_ANALYZER_H
#define LYNC_ANALYZER_H

#include "common.h"
#include "parser.h"

typedef enum {
    ALIVE,   //can be used
    MOVED,   //ownership transferred, cannot be used
    FREED,   //has been freed, cannot be used
} VarState;

typedef struct {
    TokenType type;
    char* name;
    Ownership ownership;
    VarState state;
    char* owner;
    bool is_nullable;
    bool is_const;
    bool is_dangling;
    bool is_unwrapped;
    bool is_array;
    int array_size;
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

typedef struct {
    char** imported_functions;  //array of function names that are imported
    int count;
    int capacity;
    bool has_wildcard_io;  //true if "using std.io.*" was used
} ImportRegistry;

ImportRegistry* make_import_registry();
void register_import(ImportRegistry* reg, IncludeStmt* stmt);
bool is_imported(ImportRegistry* reg, const char* func_name);

void defineAndAnalyzeFunc(FuncTable* table, Func* func);
FuncSign* lookup_func_sign(FuncTable *t, FuncSign *s);
FuncSign* lookup_func_name(FuncTable *t, char *s);

Scope* make_scope(Scope* parent);
FuncTable* make_funcTable();

void declare(Scope*, char* name, TokenType type, Ownership ownership, bool isNullable, bool isConst, bool isArray, int arraySize);
Symbol* lookup(Scope*, char* name);

TokenType analyze_expr(Scope*, FuncTable*, Expr*, FuncSign* currentFunc);
void analyze_stmt(Scope*, FuncTable*, Stmt*, FuncSign* currentFunc);
void analyze_program(Program*);

#endif //LYNC_ANALYZER_H