// created by bucka on 2/9/2026.

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
    char* type_name;             // non-NULL when type == VAR_T (struct types)
    FuncSign* fn_sig;            // non-NULL when type == FN_T (function pointer)
    Ownership ownership;
    Ownership element_ownership; // for arrays of owned pointers
    VarState state;
    char* owner;
    bool is_nullable;
    bool is_const;
    bool is_dangling;
    bool is_unwrapped;
    bool is_array;
    int array_size;
} Symbol;

// Registered struct decls. Built at the start of analyze_program from
// prog->structs[]. Field access + var-decl with struct type look up here
// to validate the type exists and to find field info.
typedef struct {
    StructDecl** decls;
    int          count;
    int          capacity;
} StructTable;

StructTable* make_struct_table();
void         register_struct(StructTable* t, StructDecl* d);
StructDecl*  lookup_struct(StructTable* t, const char* name);
StructField* lookup_field(StructDecl* d, const char* field_name);

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

#endif //lYNC_ANALYZER_H