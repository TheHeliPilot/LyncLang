//
// Created by bucka on 2/9/2026.
//

#include "analyzer.h"

Scope* make_scope(Scope* parent) {
    Scope* scope = malloc(sizeof(Scope));
    scope->capacity = 2;
    scope->symbols = malloc(sizeof(Symbol) * scope->capacity);
    scope->count = 0;
    scope->parent = parent;

    stage_trace(STAGE_ANALYZER, "created scope %p (parent=%p)", scope, parent);

    return scope;
}

void declare(Scope* scope, char* name, TokenType type, Ownership ownership) {
    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0)
            stage_error(STAGE_ANALYZER, NO_LOC,
                        "variable '%s' already declared in this scope", name);
    }

    stage_trace(STAGE_ANALYZER, "declare %s : %s",
                name, token_type_name(type));

    scope->symbols[scope->count++] =
            (Symbol){.type = type, .name = name, .ownership = ownership, .is_nullable = false, .state = ALIVE};

    if (scope->capacity == scope->count) {
        scope->capacity *= 2;
        scope->symbols =
                realloc(scope->symbols, sizeof(Symbol) * scope->capacity);
    }
}

Symbol* lookup(Scope* scope, char* name) {
    for (int i = 0; i < scope->count; ++i) {
        if (strcmp(scope->symbols[i].name, name) == 0) {
            stage_trace(STAGE_ANALYZER,
                        "lookup '%s' -> found in scope %p", name, scope);
            return &scope->symbols[i];
        }
    }

    if (scope->parent != NULL)
        return lookup(scope->parent, name);

    stage_trace(STAGE_ANALYZER, "lookup '%s' -> not found", name);
    return NULL;
}

TokenType analyze_expr(Scope* scope, FuncTable* funcTable, Expr* e) {
    switch (e->type) {
        case INT_LIT_E:
            return INT_KEYWORD_T;

        case BOOL_LIT_E:
            return BOOL_KEYWORD_T;

        case VAR_E: {
            Symbol* sym = lookup(scope, e->as.var.name);
            if (sym == nullptr)
                stage_error(STAGE_ANALYZER, e->loc, "variable '%s' is not declared", e->as.var.name);

            if (sym->ownership == OWNERSHIP_OWN && sym->state == FREED)
                stage_error(STAGE_ANALYZER, e->loc, "use after free: variable '%s' has been freed", e->as.var.name);
            if (sym->ownership == OWNERSHIP_OWN && sym->state == MOVED)
                stage_error(STAGE_ANALYZER, e->loc, "use after move: variable '%s' has been moved", e->as.var.name);

            e->as.var.ownership = sym->ownership;
            return sym->type;
        }

        case UN_OP_E: {
            TokenType operand = analyze_expr(scope, funcTable, e->as.un_op.expr);

            if (e->as.un_op.op == MINUS_T) {
                if (operand != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "unary '-' requires int, got %s", token_type_name(operand));
                return INT_KEYWORD_T;
            }
            if (e->as.un_op.op == NEGATION_T) {
                if (operand != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "'!' requires bool, got %s", token_type_name(operand));
                return BOOL_KEYWORD_T;
            }

            stage_error(STAGE_ANALYZER, e->loc, "unknown unary operator %s", token_type_name(e->as.un_op.op));
            return INT_KEYWORD_T;
        }

        case BIN_OP_E: {
            TokenType op = e->as.bin_op.op;
            TokenType left = analyze_expr(scope, funcTable, e->as.bin_op.exprL);
            TokenType right = analyze_expr(scope, funcTable, e->as.bin_op.exprR);

            // arithmetic: int op int -> int
            if (op == PLUS_T || op == MINUS_T || op == STAR_T || op == SLASH_T) {
                if (left != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be int, got %s", token_type_name(op), token_type_name(left));
                if (right != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be int, got %s", token_type_name(op), token_type_name(right));
                return INT_KEYWORD_T;
            }

            // comparison: int op int -> bool
            if (op == LESS_T || op == MORE_T || op == LESS_EQUALS_T || op == MORE_EQUALS_T) {
                if (left != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be int, got %s", token_type_name(op), token_type_name(left));
                if (right != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be int, got %s", token_type_name(op), token_type_name(right));
                return BOOL_KEYWORD_T;
            }

            // equality: same type op same type -> bool
            if (op == DOUBLE_EQUALS_T || op == NOT_EQUALS_T) {
                if (left != right)
                    stage_error(STAGE_ANALYZER, e->loc, "cannot compare %s with %s using '%s'", token_type_name(left), token_type_name(right), token_type_name(op));
                return BOOL_KEYWORD_T;
            }

            // logical: bool op bool -> bool
            if (op == AND_T || op == OR_T) {
                if (left != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be bool, got %s", token_type_name(op), token_type_name(left));
                if (right != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be bool, got %s", token_type_name(op), token_type_name(right));
                return BOOL_KEYWORD_T;
            }

            stage_error(STAGE_ANALYZER, e->loc, "unknown binary operator %s", token_type_name(op));
            return INT_KEYWORD_T;
        }
        case FUNC_CALL_E: {
            FuncSign *fsign = lookup_func_name(funcTable, e->as.func_call.name);
            if (fsign == nullptr)
                stage_error(STAGE_ANALYZER, e->loc, "Function %s not defined", e->as.func_call.name);

            if (fsign->paramNum != e->as.func_call.count)
                stage_error(STAGE_ANALYZER, e->loc, "Parameter count mismatch");

            for (int i = 0; i < fsign->paramNum; ++i) {
                TokenType exprT = analyze_expr(scope, funcTable, e->as.func_call.params[i]);

                //handle ownership transfer for 'own' parameters
                if (fsign->parameters[i].ownership == OWNERSHIP_OWN) {
                    //must be passing a variable (not a literal)
                    if (e->as.func_call.params[i]->type != VAR_E)
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc, "Can only move owned variables to 'own' parameters");

                    //get the variable symbol
                    Symbol* sym = lookup(scope, e->as.func_call.params[i]->as.var.name);

                    //must be an 'own' variable
                    if (sym->ownership != OWNERSHIP_OWN)
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc, "Cannot move non-owned variable to 'own' parameter");

                    //must be alive (not freed or already moved)
                    if (sym->state != ALIVE)
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc, "Cannot move '%s', it has been moved or freed", sym->name);

                    //transfer ownership - mark as MOVED
                    sym->state = MOVED;
                }

                //type check
                if (fsign->parameters[i].type != exprT)
                    stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc, "Parameter type mismatch");
            }
            return fsign->retType;
        }

        case FUNC_RET_E:
            return analyze_expr(scope, funcTable, e->as.func_ret_expr);
        case MATCH_E: {
            TokenType targetType = analyze_expr(scope, funcTable, e->as.match.var);

            TokenType resultType = VOID_KEYWORD_T;  // will be set by first branch

            bool hasDefault = false;

            for (int i = 0; i < e->as.match.branchCount; i++) {
                MatchBranchExpr* branch = &e->as.match.branches[i];

                // check pattern type (skip wildcard _)
                if (branch->caseExpr->type == VOID_E) {
                    hasDefault = true;
                } else {
                    TokenType patternType = analyze_expr(scope, funcTable, branch->caseExpr);
                    if (patternType != targetType)
                        stage_error(STAGE_ANALYZER, branch->caseExpr->loc,
                                    "match pattern type %s doesn't match target type %s",
                                    token_type_name(patternType), token_type_name(targetType));
                }

                // check body type
                TokenType bodyType = analyze_expr(scope, funcTable, branch->caseRet);

                if (i == 0) {
                    resultType = bodyType;
                } else if (bodyType != resultType) {
                    stage_error(STAGE_ANALYZER, branch->caseRet->loc,
                                "match branch %d returns %s but previous branches return %s",
                                i, token_type_name(bodyType), token_type_name(resultType));
                }
            }

            if (!hasDefault)
                stage_error(STAGE_ANALYZER, e->loc, "match expression must have a default '_' branch");

            return resultType;
        }
        case ALLOC_E: {
            e->as.alloc.type = analyze_expr(scope, funcTable, e->as.alloc.initialValue);
            return e->as.alloc.type;
        }
        case VOID_E:
            return VOID_KEYWORD_T;
    }

    stage_error(STAGE_ANALYZER, e->loc, "unknown expression type %d", e->type);
    return INT_KEYWORD_T;
}

void analyze_stmt(Scope* scope, FuncTable* funcTable, Stmt* s) {
    switch (s->type) {
        case VAR_DECL_S: {
            TokenType t = analyze_expr(scope, funcTable, s->as.var_decl.expr);

            if (s->as.var_decl.ownership == OWNERSHIP_OWN && s->as.var_decl.expr->type != ALLOC_E)
                stage_error(STAGE_ANALYZER, s->loc, "'own' variables must be initialized with 'alloc'");

            if (s->as.var_decl.expr->type == ALLOC_E && s->as.var_decl.ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "'alloc' can only be used with 'own' variables");

            if (t != s->as.var_decl.varType)
                stage_error(STAGE_ANALYZER, s->loc, "variable '%s' declared as %s but initialized with %s",
                      s->as.var_decl.name, token_type_name(s->as.var_decl.varType), token_type_name(t));
            declare(scope, s->as.var_decl.name, s->as.var_decl.varType, s->as.var_decl.ownership);
            break;
        }

        case ASSIGN_S: {
            Symbol* sym = lookup(scope, s->as.var_assign.name);
            s->as.var_assign.ownership = sym->ownership;
            if (sym == nullptr)
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign to '%s', variable not declared", s->as.var_assign.name);
            TokenType t = analyze_expr(scope, funcTable, s->as.var_assign.expr);
            if (t != sym->type)
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign %s to '%s' of type %s",
                      token_type_name(t), s->as.var_assign.name, token_type_name(sym->type));
            break;
        }

        case IF_S: {
            TokenType c = analyze_expr(scope, funcTable, s->as.if_stmt.cond);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "if condition must be bool, got %s", token_type_name(c));
            Scope* tScope = make_scope(scope);
            analyze_stmt(tScope, funcTable, s->as.if_stmt.trueStmt);
            free(tScope);
            if (s->as.if_stmt.falseStmt != nullptr) {
                Scope* fScope = make_scope(scope);
                analyze_stmt(fScope, funcTable, s->as.if_stmt.falseStmt);
                free(fScope);
            }
            break;
        }

        case WHILE_S: {
            TokenType c = analyze_expr(scope, funcTable, s->as.while_stmt.cond);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "while condition must be bool, got %s", token_type_name(c));
            Scope* body = make_scope(scope);
            analyze_stmt(body, funcTable, s->as.while_stmt.body);
            free(body);
            break;
        }

        case DO_WHILE_S: {
            Scope* body = make_scope(scope);
            analyze_stmt(body, funcTable, s->as.do_while_stmt.body);
            free(body);
            TokenType c = analyze_expr(scope, funcTable, s->as.do_while_stmt.cond);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "do-while condition must be bool, got %s", token_type_name(c));
            break;
        }

        case FOR_S: {
            Scope* body = make_scope(scope);
            declare(body, s->as.for_stmt.varName, INT_KEYWORD_T, OWNERSHIP_NONE);
            if (analyze_expr(body, funcTable, s->as.for_stmt.min) != INT_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "for loop min must be int");
            if (analyze_expr(body, funcTable, s->as.for_stmt.max) != INT_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "for loop max must be int");
            analyze_stmt(body, funcTable, s->as.for_stmt.body);
            free(body);
            break;
        }

        case BLOCK_S: {
            Scope* block = make_scope(scope);
            for (int i = 0; i < s->as.block_stmt.count; ++i) {
                analyze_stmt(block, funcTable, s->as.block_stmt.stmts[i]);
            }
            free(block);
            break;
        }

        case EXPR_STMT_S: {
            analyze_expr(scope, funcTable, s->as.expr_stmt);
            break;
        }

        case MATCH_S: {
            analyze_expr(scope, funcTable, s->as.match_stmt.var);
            for (int i = 0; i < s->as.match_stmt.branchCount; ++i) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];
                if (branch->caseExpr->type != VOID_E) {
                    analyze_expr(scope, funcTable, branch->caseExpr);
                }
                for (int j = 0; j < branch->stmtCount; ++j) {
                    analyze_stmt(scope, funcTable, branch->stmts[j]);
                }
            }
            break;
        }

        case FREE_S: {
            Symbol* sym = lookup(scope, s->as.free_stmt.varName);
            if (sym == nullptr)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', variable not declared", s->as.free_stmt.varName);

            // CHECK: Can only free 'own' variables
            if (sym->ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', it is not an 'own' variable", s->as.free_stmt.varName);

            // CHECK: Double free
            if (sym->state == FREED)
                stage_error(STAGE_ANALYZER, s->loc, "double free: variable '%s' has already been freed", s->as.free_stmt.varName);

            // CHECK: Free after move
            if (sym->state == MOVED)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', ownership has been moved", s->as.free_stmt.varName);

            // Mark as freed
            sym->state = FREED;
            break;
        }
    }
}

void defineAndAnalyzeFunc(FuncTable* table, Func* func) {
    //check for duplicate signature
    if(lookup_func_sign(table, func->signature)) stage_error(STAGE_ANALYZER, NO_LOC, "Function signature of function %s defined more then once", func->signature->name);

    //check main
    if(strcmp("main", func->signature->name) == 0) {
        if(func->signature->retType != INT_KEYWORD_T) stage_error(STAGE_ANALYZER, NO_LOC, "Main function needs to have return type of int!");
        if(func->signature->paramNum > 0) stage_error(STAGE_ANALYZER, NO_LOC, "Main function does not take any parameters!");
    }

    //add to table
    table->signs[table->count++] = *func->signature; //should i make a copy? not sure
    if(table->count >= table->capacity) {
        table->capacity *= 2;
        table->signs = realloc(table->signs, sizeof(FuncSign) * table->capacity);
    }
}

FuncSign* lookup_func_sign(FuncTable* t, FuncSign* s) {
    for (int i = 0; i < t->count; ++i) {
        if(checkFuncSign(&t->signs[i], s))
            return s;
    }
    return nullptr;
}
FuncSign* lookup_func_name(FuncTable* t, char* n) {
    for (int i = 0; i < t->count; ++i) {
        if(strcmp(t->signs[i].name, n) == 0)
            return &t->signs[i];
    }
    return nullptr;
}

FuncTable* make_funcTable() {
    FuncTable* f = malloc(sizeof(FuncTable));
    f->signs = malloc(sizeof(FuncSign) * 2);
    f->capacity = 2;
    f->count = 0;
    return f;
}

void check_function_cleanup(Scope* scope) {
    for (int i = 0; i < scope->count; i++) {
        Symbol* sym = &scope->symbols[i];
        if (sym->ownership == OWNERSHIP_OWN && sym->state == ALIVE) {
            stage_error(STAGE_ANALYZER, NO_LOC,
                        "memory leak: 'own' variable '%s' was not freed or moved before function end",
                        sym->name);
        }
    }
}

void analyze_program(Func** fs, int count) {
    Scope* global = make_scope(nullptr);
    FuncTable* funcTable = make_funcTable();

    for (int i = 0; i < count; ++i) {
        defineAndAnalyzeFunc(funcTable, fs[i]);
    }
    for (int i = 0; i < count; ++i) {
        Scope* funcScope = make_scope(global);

        for (int j = 0; j < fs[i]->signature->paramNum; ++j) {
            declare(funcScope, fs[i]->signature->parameters[j].name, fs[i]->signature->parameters[j].type, fs[i]->signature->parameters[j].ownership);
        }

        analyze_stmt(funcScope, funcTable, fs[i]->body);

        check_function_cleanup(funcScope);

        free(funcScope->symbols);
        free(funcScope);
    }

    free(funcTable->signs);
    free(funcTable);

    free(global->symbols);
    free(global);
}