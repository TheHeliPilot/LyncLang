//
// Created by bucka on 2/12/2026.
//

#ifndef LYNC_OPTIMIZER_H
#define LYNC_OPTIMIZER_H

#include "common.h"
#include "parser.h"
#include "analyzer.h"

typedef enum {
    OPT_NONE = 0,
    OPT_CONST_FOLD = 1 << 0,
    OPT_DEAD_CODE = 1 << 1,
    OPT_PEEPHOLE = 1 << 2,
    OPT_INLINE = 1 << 3,
    OPT_ALL = 0xFF
} OptimizationLevel;

void optimize_program(Func** program, int count, OptimizationLevel level);

bool constant_folding(Func** program, int count);
bool dead_code_elimination(Func** program, int count);
bool peephole_optimizations(Func** program, int count);
bool inline_functions(Func** program, int count);

bool is_constant_expr(Expr* e);
int eval_constant_expr(Expr* e);
bool is_constant_true(Expr* e);
bool is_constant_false(Expr* e);
bool is_small_function(Func* f);

#endif //LYNC_OPTIMIZER_H