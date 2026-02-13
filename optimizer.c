//
// Created by bucka on 2/12/2026.
//

#include "optimizer.h"
#include "common.h"

// ============ CONSTANT FOLDING ============

bool is_constant_expr(Expr* e) {
    if (!e) return false;
    switch (e->type) {
        case INT_LIT_E:
        case BOOL_LIT_E:
        case STR_LIT_E:
            return true;
        case UN_OP_E:
            return is_constant_expr(e->as.un_op.expr);
        case BIN_OP_E:
            return is_constant_expr(e->as.bin_op.exprL) &&
                   is_constant_expr(e->as.bin_op.exprR);
        default:
            return false;
    }
}

int eval_constant_expr(Expr* e) {
    if (!e) return 0;
    switch (e->type) {
        case INT_LIT_E: return e->as.int_val;
        case BOOL_LIT_E: return e->as.bool_val;
        case UN_OP_E: {
            int val = eval_constant_expr(e->as.un_op.expr);
            if (e->as.un_op.op == MINUS_T) return -val;
            if (e->as.un_op.op == NEGATION_T) return !val;
            return val;
        }
        case BIN_OP_E: {
            int left = eval_constant_expr(e->as.bin_op.exprL);
            int right = eval_constant_expr(e->as.bin_op.exprR);
            switch (e->as.bin_op.op) {
                case PLUS_T: return left + right;
                case MINUS_T: return left - right;
                case STAR_T: return left * right;
                case SLASH_T: return right != 0 ? left / right : 0;
                case LESS_T: return left < right;
                case MORE_T: return left > right;
                case LESS_EQUALS_T: return left <= right;
                case MORE_EQUALS_T: return left >= right;
                case DOUBLE_EQUALS_T: return left == right;
                case NOT_EQUALS_T: return left != right;
                case AND_T: return left && right;
                case OR_T: return left || right;
                default: return 0;
            }
        }
        default: return 0;
    }
}

//returns true if expression was modified
bool fold_expression(Expr** e_ptr) {
    if (!e_ptr || !*e_ptr) return false;

    Expr* e = *e_ptr;
    bool modified = false;

    if (e->type == UN_OP_E) {
        modified |= fold_expression(&e->as.un_op.expr);
    } else if (e->type == BIN_OP_E) {
        modified |= fold_expression(&e->as.bin_op.exprL);
        modified |= fold_expression(&e->as.bin_op.exprR);
    }

    if (is_constant_expr(e) && e->analyzedType != STR_KEYWORD_T) {
        int value = eval_constant_expr(e);

        if (e->analyzedType == INT_KEYWORD_T) {
            if (e->type != INT_LIT_E || e->as.int_val != value) {
                e->type = INT_LIT_E;
                e->as.int_val = value;
                modified = true;
            }
        } else if (e->analyzedType == BOOL_KEYWORD_T) {
            if (e->type != BOOL_LIT_E || e->as.bool_val != value) {
                e->type = BOOL_LIT_E;
                e->as.bool_val = value;
                modified = true;
            }
        }
    }

    return modified;
}

//returns true if statement was modified
bool constant_folding_stmt(Stmt* s) {
    if (!s) return false;

    bool modified = false;

    switch (s->type) {
        case VAR_DECL_S:
            modified |= fold_expression(&s->as.var_decl.expr);
            break;
        case ASSIGN_S:
            modified |= fold_expression(&s->as.var_assign.expr);
            break;
        case IF_S:
            modified |= fold_expression(&s->as.if_stmt.cond);
            modified |= constant_folding_stmt(s->as.if_stmt.trueStmt);
            modified |= constant_folding_stmt(s->as.if_stmt.falseStmt);
            break;
        case WHILE_S:
            modified |= fold_expression(&s->as.while_stmt.cond);
            modified |= constant_folding_stmt(s->as.while_stmt.body);
            break;
        case DO_WHILE_S:
            modified |= constant_folding_stmt(s->as.do_while_stmt.body);
            modified |= fold_expression(&s->as.do_while_stmt.cond);
            break;
        case FOR_S:
            modified |= fold_expression(&s->as.for_stmt.min);
            modified |= fold_expression(&s->as.for_stmt.max);
            modified |= constant_folding_stmt(s->as.for_stmt.body);
            break;
        case BLOCK_S:
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                modified |= constant_folding_stmt(s->as.block_stmt.stmts[i]);
            }
            break;
        case EXPR_STMT_S:
            modified |= fold_expression(&s->as.expr_stmt);
            break;
        case MATCH_S:
            modified |= fold_expression(&s->as.match_stmt.var);
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];
                // For VALUE_PATTERN, fold the expression
                if (branch->pattern->type == VALUE_PATTERN) {
                    modified |= fold_expression(&branch->pattern->as.value_expr);
                }
                for (int j = 0; j < branch->stmtCount; j++) {
                    modified |= constant_folding_stmt(branch->stmts[j]);
                }
            }
            break;
        default:
            break;
    }

    return modified;
}

bool constant_folding(Func** program, int count) {
    stage_trace(STAGE_OPTIMIZER, "Running constant folding...");
    bool any_modified = false;
    for (int i = 0; i < count; i++) {
        any_modified |= constant_folding_stmt(program[i]->body);
    }
    if (any_modified) {
        stage_trace(STAGE_OPTIMIZER, "Constant folding made changes");
    }
    return any_modified;
}

// ============ DEAD CODE ELIMINATION ============

bool is_constant_true(Expr* e) {
    return e && e->type == BOOL_LIT_E && e->as.bool_val == 1;
}

bool is_constant_false(Expr* e) {
    return e && e->type == BOOL_LIT_E && e->as.bool_val == 0;
}

//returns true if statement was modified
bool dead_code_elimination_stmt(Stmt** s_ptr) {
    if (!s_ptr || !*s_ptr) return false;

    Stmt* s = *s_ptr;
    bool modified = false;

    switch (s->type) {
        case IF_S: {
            modified |= fold_expression(&s->as.if_stmt.cond);

            if (is_constant_true(s->as.if_stmt.cond)) {
                Stmt* result = s->as.if_stmt.trueStmt;
                s->as.if_stmt.trueStmt = NULL;  //prevent double free
                free(s);
                *s_ptr = result;
                return true;  //definitely modified
            } else if (is_constant_false(s->as.if_stmt.cond)) {
                Stmt* result = s->as.if_stmt.falseStmt;
                s->as.if_stmt.falseStmt = NULL;
                free(s);
                *s_ptr = result;
                return true;  //definitely modified
            }

            modified |= dead_code_elimination_stmt(&s->as.if_stmt.trueStmt);
            modified |= dead_code_elimination_stmt(&s->as.if_stmt.falseStmt);
            break;
        }

        case WHILE_S: {
            modified |= fold_expression(&s->as.while_stmt.cond);

            if (is_constant_false(s->as.while_stmt.cond)) {
                free(s);
                *s_ptr = NULL;
                return true;
            }
            modified |= dead_code_elimination_stmt(&s->as.while_stmt.body);
            break;
        }

        case BLOCK_S: {
            //filter out NULL statements
            int new_count = 0;
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                modified |= dead_code_elimination_stmt(&s->as.block_stmt.stmts[i]);
                if (s->as.block_stmt.stmts[i]) {
                    s->as.block_stmt.stmts[new_count++] = s->as.block_stmt.stmts[i];
                }
            }
            if (new_count != s->as.block_stmt.count) {
                s->as.block_stmt.count = new_count;
                modified = true;
            }
            break;
        }

        case DO_WHILE_S:
            modified |= dead_code_elimination_stmt(&s->as.do_while_stmt.body);
            modified |= fold_expression(&s->as.do_while_stmt.cond);
            break;

        case FOR_S:
            modified |= fold_expression(&s->as.for_stmt.min);
            modified |= fold_expression(&s->as.for_stmt.max);
            modified |= dead_code_elimination_stmt(&s->as.for_stmt.body);
            break;

        default:
            break;
    }

    return modified;
}

bool dead_code_elimination(Func** program, int count) {
    stage_trace(STAGE_OPTIMIZER, "Running dead code elimination...");
    bool any_modified = false;
    for (int i = 0; i < count; i++) {
        any_modified |= dead_code_elimination_stmt(&program[i]->body);
    }
    if (any_modified) {
        stage_trace(STAGE_OPTIMIZER, "Dead code elimination made changes");
    }
    return any_modified;
}

// ============ PEEPHOLE OPTIMIZATIONS ============

//returns true if expression was modified
bool peephole_optimize_expr(Expr** e_ptr) {
    if (!e_ptr || !*e_ptr) return false;

    Expr* e = *e_ptr;
    bool modified = false;

    if (e->type == UN_OP_E) {
        modified |= peephole_optimize_expr(&e->as.un_op.expr);
    } else if (e->type == BIN_OP_E) {
        modified |= peephole_optimize_expr(&e->as.bin_op.exprL);
        modified |= peephole_optimize_expr(&e->as.bin_op.exprR);

        // x + 0 -> x
        if (e->as.bin_op.op == PLUS_T) {
            if (e->as.bin_op.exprR->type == INT_LIT_E && e->as.bin_op.exprR->as.int_val == 0) {
                *e_ptr = e->as.bin_op.exprL;
                e->as.bin_op.exprL = NULL;
                free(e);
                return true;
            }
            if (e->as.bin_op.exprL->type == INT_LIT_E && e->as.bin_op.exprL->as.int_val == 0) {
                *e_ptr = e->as.bin_op.exprR;
                e->as.bin_op.exprR = NULL;
                free(e);
                return true;
            }
        }

        // x * 1 -> x
        if (e->as.bin_op.op == STAR_T) {
            if (e->as.bin_op.exprR->type == INT_LIT_E && e->as.bin_op.exprR->as.int_val == 1) {
                *e_ptr = e->as.bin_op.exprL;
                e->as.bin_op.exprL = NULL;
                free(e);
                return true;
            }
            if (e->as.bin_op.exprL->type == INT_LIT_E && e->as.bin_op.exprL->as.int_val == 1) {
                *e_ptr = e->as.bin_op.exprR;
                e->as.bin_op.exprR = NULL;
                free(e);
                return true;
            }
        }

        // x - 0 -> x
        if (e->as.bin_op.op == MINUS_T) {
            if (e->as.bin_op.exprR->type == INT_LIT_E && e->as.bin_op.exprR->as.int_val == 0) {
                *e_ptr = e->as.bin_op.exprL;
                e->as.bin_op.exprL = NULL;
                free(e);
                return true;
            }
        }

        // x / 1 -> x
        if (e->as.bin_op.op == SLASH_T) {
            if (e->as.bin_op.exprR->type == INT_LIT_E && e->as.bin_op.exprR->as.int_val == 1) {
                *e_ptr = e->as.bin_op.exprL;
                e->as.bin_op.exprL = NULL;
                free(e);
                return true;
            }
        }

        // x - x -> 0
        if (e->as.bin_op.op == MINUS_T) {
            //for now, just handle same variable
            if (e->as.bin_op.exprL->type == VAR_E && e->as.bin_op.exprR->type == VAR_E &&
                strcmp(e->as.bin_op.exprL->as.var.name, e->as.bin_op.exprR->as.var.name) == 0) {
                e->type = INT_LIT_E;
                e->as.int_val = 0;
                e->analyzedType = INT_KEYWORD_T;
                free(e->as.bin_op.exprL);
                free(e->as.bin_op.exprR);
                return true;
            }
        }
    }

    //double negation: !!x -> x
    if (e->type == UN_OP_E && e->as.un_op.op == NEGATION_T) {
        if (e->as.un_op.expr->type == UN_OP_E && e->as.un_op.expr->as.un_op.op == NEGATION_T) {
            *e_ptr = e->as.un_op.expr->as.un_op.expr;
            e->as.un_op.expr->as.un_op.expr = NULL;
            free(e->as.un_op.expr);
            free(e);
            return true;
        }
    }

    // !(x < y) -> x >= y
    if (e->type == UN_OP_E && e->as.un_op.op == NEGATION_T) {
        if (e->as.un_op.expr->type == BIN_OP_E) {
            Expr* inner = e->as.un_op.expr;
            TokenType op = inner->as.bin_op.op;
            TokenType new_op = 0;

            switch (op) {
                case LESS_T: new_op = MORE_EQUALS_T; break;
                case MORE_T: new_op = LESS_EQUALS_T; break;
                case LESS_EQUALS_T: new_op = MORE_T; break;
                case MORE_EQUALS_T: new_op = LESS_T; break;
                case DOUBLE_EQUALS_T: new_op = NOT_EQUALS_T; break;
                case NOT_EQUALS_T: new_op = DOUBLE_EQUALS_T; break;
                default: break;
            }

            if (new_op != 0) {
                e->type = BIN_OP_E;
                e->as.bin_op.op = new_op;
                e->as.bin_op.exprL = inner->as.bin_op.exprL;
                e->as.bin_op.exprR = inner->as.bin_op.exprR;
                e->analyzedType = BOOL_KEYWORD_T;
                inner->as.bin_op.exprL = NULL;
                inner->as.bin_op.exprR = NULL;
                free(inner);
                return true;
            }
        }
    }

    return modified;
}

bool peephole_optimizations_stmt(Stmt* s) {
    if (!s) return false;

    bool modified = false;

    switch (s->type) {
        case VAR_DECL_S:
            modified |= peephole_optimize_expr(&s->as.var_decl.expr);
            break;
        case ASSIGN_S:
            modified |= peephole_optimize_expr(&s->as.var_assign.expr);
            break;
        case IF_S:
            modified |= peephole_optimize_expr(&s->as.if_stmt.cond);
            modified |= peephole_optimizations_stmt(s->as.if_stmt.trueStmt);
            modified |= peephole_optimizations_stmt(s->as.if_stmt.falseStmt);
            break;
        case WHILE_S:
            modified |= peephole_optimize_expr(&s->as.while_stmt.cond);
            modified |= peephole_optimizations_stmt(s->as.while_stmt.body);
            break;
        case DO_WHILE_S:
            modified |= peephole_optimizations_stmt(s->as.do_while_stmt.body);
            modified |= peephole_optimize_expr(&s->as.do_while_stmt.cond);
            break;
        case FOR_S:
            modified |= peephole_optimize_expr(&s->as.for_stmt.min);
            modified |= peephole_optimize_expr(&s->as.for_stmt.max);
            modified |= peephole_optimizations_stmt(s->as.for_stmt.body);
            break;
        case BLOCK_S:
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                modified |= peephole_optimizations_stmt(s->as.block_stmt.stmts[i]);
            }
            break;
        case EXPR_STMT_S:
            modified |= peephole_optimize_expr(&s->as.expr_stmt);
            break;
        case MATCH_S:
            modified |= peephole_optimize_expr(&s->as.match_stmt.var);
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];
                //for VALUE_PATTERN, optimize the expression
                if (branch->pattern->type == VALUE_PATTERN) {
                    modified |= peephole_optimize_expr(&branch->pattern->as.value_expr);
                }
                for (int j = 0; j < branch->stmtCount; j++) {
                    modified |= peephole_optimizations_stmt(branch->stmts[j]);
                }
            }
            break;
        default:
            break;
    }

    return modified;
}

bool peephole_optimizations(Func** program, int count) {
    stage_trace(STAGE_OPTIMIZER, "Running peephole optimizations...");
    bool any_modified = false;
    for (int i = 0; i < count; i++) {
        any_modified |= peephole_optimizations_stmt(program[i]->body);
    }
    if (any_modified) {
        stage_trace(STAGE_OPTIMIZER, "Peephole optimizations made changes");
    }
    return any_modified;
}

// ============ INLINING ============

bool is_small_function(Func* f) {
    //count statements roughly
    int count = 0;
    Stmt* body = f->body;
    if (body->type == BLOCK_S) {
        count = body->as.block_stmt.count;
    }
    return count <= 5;
}

bool inline_functions(Func** program, int count) {
    stage_trace(STAGE_OPTIMIZER, "Running function inlining...");
    // TODO: Implement function inlining
    return false;  //no changes yet
}

// ============ MAIN OPTIMIZATION DRIVER ============

void optimize_program(Func** program, int count, OptimizationLevel level) {
    if (level == OPT_NONE) return;

    stage_trace(STAGE_OPTIMIZER, "Optimizing with level %d", level);

    //run multiple passes until no changes
    bool changed;
    int pass = 0;
    do {
        changed = false;
        pass++;
        stage_trace(STAGE_OPTIMIZER, "Optimization pass %d", pass);

        if (level & OPT_CONST_FOLD) {
            changed |= constant_folding(program, count);
        }

        if (level & OPT_PEEPHOLE) {
            changed |= peephole_optimizations(program, count);
        }

        if (level & OPT_DEAD_CODE) {
            changed |= dead_code_elimination(program, count);
        }

        if (level & OPT_INLINE) {
            changed |= inline_functions(program, count);
        }

    } while (changed && pass < 10);

    if (pass >= 10) {
        stage_trace(STAGE_OPTIMIZER, "Reached maximum optimization passes (10)");
    }
}