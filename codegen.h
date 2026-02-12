//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_CODEGEN_H
#define LYNC_CODEGEN_H

#include "common.h"
#include "parser.h"

typedef struct {
    char* name;
    int count;
} FuncNameCounterElement;
typedef struct {
    FuncNameCounterElement* elements;
    int height;
    int count;
} FuncNameCounter;

typedef struct {
    FuncSign* sign;
    char* name;
} FuncSignToNameElement;
typedef struct {
    FuncSignToNameElement* elements;
    int height;
    int count;
} FuncSignToName;

void generate_code(Func** program, int count, FILE* output);
void generate_assembly(Func** program, int count, FILE* output);

void emit_expr(Expr* e, FILE* out, FuncSignToName*);
void emit_stmt(Stmt* s, FILE* out, int indent_level, FuncSignToName*);
void emit_indent(FILE* out, int level);
void emit_func(Func* f, FILE* out, FuncSignToName*);
void emit_func_decl(Func* f, FILE* out, FuncNameCounter*, FuncSignToName*);
void emit_assign_expr_to_var(Expr* e, const char* targetVar, Ownership, FILE* out, int indent, FuncSignToName*);

char* type_to_c_type(TokenType t);

#endif //LYNC_CODEGEN_H
