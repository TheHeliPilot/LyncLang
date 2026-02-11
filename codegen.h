//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_CODEGEN_H
#define LYNC_CODEGEN_H

#include "common.h"
#include "parser.h"

void generate_code(Func** program, int count, FILE* output);

void emit_expr(Expr* e, FILE* out);
void emit_stmt(Stmt* s, FILE* out, int indent_level);
void emit_indent(FILE* out, int level);
void emit_func(Func* f, FILE* out);
void emit_func_decl(Func* f, FILE* out);
void emit_assign_expr_to_var(Expr* e, const char* targetVar, Ownership, FILE* out, int indent);

char* type_to_c_type(TokenType t);

#endif //LYNC_CODEGEN_H
