//
// Created by bucka on 2/9/2026.
//

#include "analyzer.h"

// Forward declaration
void check_function_cleanup(Scope* scope);

Scope* make_scope(Scope* parent) {
    Scope* scope = malloc(sizeof(Scope));
    scope->capacity = 2;
    scope->symbols = malloc(sizeof(Symbol) * scope->capacity);
    scope->count = 0;
    scope->parent = parent;

    stage_trace(STAGE_ANALYZER, "created scope %p (parent=%p)", scope, parent);

    return scope;
}

void declare(Scope* scope, char* name, TokenType type, Ownership ownership, bool isNullable) {
    // Check if trying to declare 'print' as a variable
    if (strcmp(name, "print") == 0) {
        stage_error(STAGE_ANALYZER, NO_LOC,
                    "'print' is a reserved built-in function and cannot be used as a variable name");
    }

    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0)
            stage_error(STAGE_ANALYZER, NO_LOC,
                        "variable '%s' already declared in this scope", name);
    }

    stage_trace(STAGE_ANALYZER, "declare %s : %s%s",
                name, isNullable ? "nullable " : "", token_type_name(type));

    scope->symbols[scope->count++] =
            (Symbol){.type = type, .name = name, .ownership = ownership, .is_nullable = isNullable, .state = ALIVE, .is_unwrapped = false};

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

void mark_dangling_refs(Scope* scope, char* owner_name) {
    for (int i = 0; i < scope->count; i++) {
        Symbol* sym = &scope->symbols[i];
        if (sym->ownership == OWNERSHIP_REF &&
            sym->owner &&
            strcmp(sym->owner, owner_name) == 0) {
            sym->is_dangling = true;
        }
    }

    if (scope->parent) {
        mark_dangling_refs(scope->parent, owner_name);
    }
}

TokenType analyze_expr(Scope* scope, FuncTable* funcTable, Expr* e) {
    TokenType result;

    switch (e->type) {
        case INT_LIT_E:
            result = INT_KEYWORD_T;
            break;

        case BOOL_LIT_E:
            result = BOOL_KEYWORD_T;
            break;

        case STR_LIT_E:
            result = STR_KEYWORD_T;
            break;

        case NULL_LIT_E:
            result = NULL_LIT_T;
            break;

        case VAR_E: {
            Symbol* sym = lookup(scope, e->as.var.name);
            if (sym == nullptr) {
                // Check if trying to use 'print' as a variable (give better error message)
                if (strcmp(e->as.var.name, "print") == 0) {
                    stage_error(STAGE_ANALYZER, e->loc, "'print' is a built-in function, not a variable (use print(...) to call it)");
                } else {
                    stage_error(STAGE_ANALYZER, e->loc, "variable '%s' is not declared", e->as.var.name);
                }
                result = VOID_KEYWORD_T;
                break;
            }

            if (sym->ownership == OWNERSHIP_OWN && sym->state == FREED)
                stage_error(STAGE_ANALYZER, e->loc, "use after free: variable '%s' has been freed", e->as.var.name);
            if (sym->ownership == OWNERSHIP_OWN && sym->state == MOVED)
                stage_error(STAGE_ANALYZER, e->loc, "use after move: variable '%s' has been moved", e->as.var.name);
            if (sym->ownership == OWNERSHIP_REF && lookup(scope, sym->owner)->state != ALIVE)
                stage_error(STAGE_ANALYZER, e->loc, "use after owner no longer in scope: owner '%s' of '%s' is out of scope", lookup(scope, sym->owner)->name, e->as.var.name);

            // Check for nullable usage in expressions (only allowed if unwrapped)
            if (sym->is_nullable && !sym->is_unwrapped) {
                stage_error(STAGE_ANALYZER, e->loc,
                    "nullable variable '%s' must be unwrapped before use",
                    e->as.var.name);
                stage_note(STAGE_ANALYZER, e->loc,
                    "use 'match %s { some(val): { ... } null: { ... } }' to safely unwrap, or 'if(some(%s))' to check",
                    e->as.var.name, e->as.var.name);
            }

            e->as.var.ownership = sym->ownership;
            result = sym->type;
            break;
        }

        case UN_OP_E: {
            TokenType operand = analyze_expr(scope, funcTable, e->as.un_op.expr);

            if (e->as.un_op.op == MINUS_T) {
                if (operand != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "unary '-' requires int, got %s", token_type_name(operand));
                result = INT_KEYWORD_T;
            } else if (e->as.un_op.op == NEGATION_T) {
                if (operand != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "'!' requires bool, got %s", token_type_name(operand));
                result = BOOL_KEYWORD_T;
            } else {
                stage_error(STAGE_ANALYZER, e->loc, "unknown unary operator %s", token_type_name(e->as.un_op.op));
                result = INT_KEYWORD_T;
            }
            break;
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
                result = INT_KEYWORD_T;
            }
            // comparison: int op int -> bool
            else if (op == LESS_T || op == MORE_T || op == LESS_EQUALS_T || op == MORE_EQUALS_T) {
                if (left != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be int, got %s", token_type_name(op), token_type_name(left));
                if (right != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be int, got %s", token_type_name(op), token_type_name(right));
                result = BOOL_KEYWORD_T;
            }
            // equality: same type op same type -> bool
            else if (op == DOUBLE_EQUALS_T || op == NOT_EQUALS_T) {
                if (left != right)
                    stage_error(STAGE_ANALYZER, e->loc, "cannot compare %s with %s using '%s'", token_type_name(left), token_type_name(right), token_type_name(op));
                result = BOOL_KEYWORD_T;
            }
            // logical: bool op bool -> bool
            else if (op == AND_T || op == OR_T) {
                if (left != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be bool, got %s", token_type_name(op), token_type_name(left));
                if (right != BOOL_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be bool, got %s", token_type_name(op), token_type_name(right));
                result = BOOL_KEYWORD_T;
            } else {
                stage_error(STAGE_ANALYZER, e->loc, "unknown binary operator %s", token_type_name(op));
                result = INT_KEYWORD_T;
            }
            break;
        }
        case FUNC_CALL_E: {
            // Handle built-in 'print' function
            if(strcmp(e->as.func_call.name, "print") == 0) {
                // Type-check each argument
                for (int i = 0; i < e->as.func_call.count; ++i) {
                    TokenType argType = analyze_expr(scope, funcTable, e->as.func_call.params[i]);
                    if (argType != INT_KEYWORD_T && argType != BOOL_KEYWORD_T && argType != STR_KEYWORD_T) {
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc,
                                    "print only supports int, bool, and string, got %s",
                                    token_type_name(argType));
                    }
                }
                if (e->as.func_call.count == 0) {
                    stage_warning(STAGE_ANALYZER, e->loc, "print called with no arguments");
                }
                e->as.func_call.resolved_sign = NULL;
                result = VOID_KEYWORD_T;
                break;
            }

            // Analyze all arguments first
            TokenType* argTypes = malloc(sizeof(TokenType) * e->as.func_call.count);
            for (int i = 0; i < e->as.func_call.count; ++i) {
                argTypes[i] = analyze_expr(scope, funcTable, e->as.func_call.params[i]);
            }

            // Find ALL matching overloads by name and arity
            FuncSign* matches[funcTable->count];
            int matchCount = 0;

            for (int i = 0; i < funcTable->count; i++) {
                FuncSign* candidate = &funcTable->signs[i];
                if (strcmp(candidate->name, e->as.func_call.name) == 0 &&
                    candidate->paramNum == e->as.func_call.count) {
                    matches[matchCount++] = candidate;
                }
            }

            if (matchCount == 0) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "no function '%s' takes %d arguments",
                            e->as.func_call.name, e->as.func_call.count);
                free(argTypes);
                e->as.func_call.resolved_sign = NULL;
                result = VOID_KEYWORD_T;
                break;
            }

            // Find exact type match
            FuncSign* match = NULL;
            for (int i = 0; i < matchCount; i++) {
                FuncSign* candidate = matches[i];
                bool typesMatch = true;
                for (int j = 0; j < candidate->paramNum; j++) {
                    if (candidate->parameters[j].type != argTypes[j]) {
                        typesMatch = false;
                        break;
                    }
                }
                if (typesMatch) {
                    match = candidate;
                    break;
                }
            }

            if (match == NULL) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "no matching overload for '%s'", e->as.func_call.name);

                // Show argument types as a note
                char arg_buffer[256];
                char* ptr = arg_buffer;
                ptr += sprintf(ptr, "argument types: (");
                for (int i = 0; i < e->as.func_call.count; i++) {
                    if (i > 0) ptr += sprintf(ptr, ", ");
                    ptr += sprintf(ptr, "%s", token_type_name(argTypes[i]));
                }
                ptr += sprintf(ptr, ")");
                stage_note(STAGE_ANALYZER, e->loc, "%s", arg_buffer);

                // Show each candidate as a separate note
                for (int i = 0; i < matchCount; i++) {
                    FuncSign* candidate = matches[i];
                    char cand_buffer[256];
                    char* cp = cand_buffer;
                    cp += sprintf(cp, "candidate: %s(", candidate->name);
                    for (int j = 0; j < candidate->paramNum; j++) {
                        if (j > 0) cp += sprintf(cp, ", ");
                        cp += sprintf(cp, "%s", token_type_name(candidate->parameters[j].type));
                    }
                    cp += sprintf(cp, ") -> %s", token_type_name(candidate->retType));
                    stage_note(STAGE_ANALYZER, e->loc, "%s", cand_buffer);
                }

                free(argTypes);
                e->as.func_call.resolved_sign = NULL;
                result = VOID_KEYWORD_T;
                break;
            }

            // Store the resolved signature
            e->as.func_call.resolved_sign = match;

            // Handle ownership transfer for 'own' parameters
            for (int i = 0; i < match->paramNum; ++i) {
                if (match->parameters[i].ownership == OWNERSHIP_OWN) {
                    if (e->as.func_call.params[i]->type != VAR_E) {
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc,
                                    "can only move owned variables to 'own' parameters");
                        continue;
                    }
                    Symbol* sym = lookup(scope, e->as.func_call.params[i]->as.var.name);
                    if (sym == nullptr) continue;
                    if (sym->ownership != OWNERSHIP_OWN) {
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc,
                                    "cannot move non-owned variable to 'own' parameter");
                        continue;
                    }
                    if (sym->state != ALIVE) {
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc,
                                    "cannot move '%s', it has been moved or freed", sym->name);
                        continue;
                    }
                    sym->state = MOVED;
                }
            }

            result = match->retType;
            free(argTypes);
            break;
        }

        case FUNC_RET_E:
            result = analyze_expr(scope, funcTable, e->as.func_ret_expr);
            break;

        case MATCH_E: {
            // Get the matched variable (special handling for nullable to bypass unwrap check)
            Symbol* matchedSym = nullptr;
            TokenType targetType = VOID_KEYWORD_T;

            if (e->as.match.var->type == VAR_E) {
                matchedSym = lookup(scope, e->as.match.var->as.var.name);
                if (matchedSym) {
                    targetType = matchedSym->type;
                } else {
                    stage_error(STAGE_ANALYZER, e->loc, "variable '%s' is not declared",
                               e->as.match.var->as.var.name);
                }
            } else {
                // For non-variable expressions, analyze normally
                targetType = analyze_expr(scope, funcTable, e->as.match.var);
            }

            bool isNullableMatch = (matchedSym && matchedSym->is_nullable);
            bool hasDefault = false;
            bool hasSome = false, hasNull = false;
            TokenType resultType = VOID_KEYWORD_T;

            for (int i = 0; i < e->as.match.branchCount; i++) {
                MatchBranchExpr* branch = &e->as.match.branches[i];

                // Validate pattern types
                if (branch->pattern->type == SOME_PATTERN) {
                    hasSome = true;
                    if (!isNullableMatch)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                            "some() pattern can only be used on nullable types");
                }
                else if (branch->pattern->type == NULL_PATTERN) {
                    hasNull = true;
                    if (!isNullableMatch)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                            "null pattern can only be used on nullable types");
                }
                else if (branch->pattern->type == WILDCARD_PATTERN) {
                    hasDefault = true;
                    hasSome = hasNull = true;
                }
                else if (branch->pattern->type == VALUE_PATTERN) {
                    TokenType patternType = analyze_expr(scope, funcTable, branch->pattern->as.value_expr);
                    if (patternType != targetType)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                                    "match pattern type %s doesn't match target type %s",
                                    token_type_name(patternType), token_type_name(targetType));
                }

                // For SOME_PATTERN in expression match, create a temporary scope with the binding
                Scope* branchScope = scope;
                if (branch->pattern->type == SOME_PATTERN && matchedSym) {
                    branchScope = make_scope(scope);

                    // Binding is always a reference (borrows from original)
                    Ownership bindingOwnership = (matchedSym->ownership == OWNERSHIP_NONE)
                        ? OWNERSHIP_NONE
                        : OWNERSHIP_REF;

                    declare(branchScope,
                           branch->pattern->as.binding_name,
                           matchedSym->type,
                           bindingOwnership,
                           false);  // binding is NOT nullable

                    // Set owner for ref tracking
                    if (bindingOwnership == OWNERSHIP_REF) {
                        Symbol* bindingSym = lookup(branchScope, branch->pattern->as.binding_name);
                        if (bindingSym) {
                            bindingSym->owner = matchedSym->name;
                        }
                    }

                    // Store type for codegen
                    branch->analyzed_type = matchedSym->type;

                    // Mark original variable as unwrapped in this branch scope
                    Symbol* origSym = lookup(branchScope, matchedSym->name);
                    if (origSym) {
                        origSym->is_unwrapped = true;
                    }
                }

                TokenType bodyType = analyze_expr(branchScope, funcTable, branch->caseRet);

                // Clean up temporary scope
                if (branchScope != scope) {
                    free(branchScope);
                }

                if (i == 0) {
                    resultType = bodyType;
                } else if (bodyType != resultType) {
                    stage_error(STAGE_ANALYZER, branch->caseRet->loc,
                                "match branch %d returns %s but previous branches return %s",
                                i, token_type_name(bodyType), token_type_name(resultType));
                }
            }

            if (isNullableMatch && (!hasSome || !hasNull)) {
                stage_error(STAGE_ANALYZER, e->loc,
                    "match on nullable type must handle both some and null cases");
            }

            if (!hasDefault && !isNullableMatch)
                stage_error(STAGE_ANALYZER, e->loc, "match expression must have a default '_' branch");

            result = resultType;
            break;
        }
        case SOME_E: {
            // some() is allowed to access nullable variables without unwrapping
            // Just verify the variable exists and is nullable
            if (e->as.some.var->type == VAR_E) {
                Symbol* sym = lookup(scope, e->as.some.var->as.var.name);
                if (sym == nullptr) {
                    stage_error(STAGE_ANALYZER, e->loc, "variable '%s' is not declared", e->as.some.var->as.var.name);
                } else if (!sym->is_nullable) {
                    stage_warning(STAGE_ANALYZER, e->loc,
                        "some() used on non-nullable variable '%s' (always true if not null)",
                        e->as.some.var->as.var.name);
                }
            } else {
                // If it's not a simple variable, analyze it normally
                analyze_expr(scope, funcTable, e->as.some.var);
            }
            result = BOOL_KEYWORD_T;
            break;
        }
        case ALLOC_E: {
            e->as.alloc.type = analyze_expr(scope, funcTable, e->as.alloc.initialValue);
            result = e->as.alloc.type;
            break;
        }
        case VOID_E:
            result = VOID_KEYWORD_T;
            break;

        default:
            stage_error(STAGE_ANALYZER, e->loc, "unknown expression type %d", e->type);
            result = INT_KEYWORD_T;
            break;
    }

    // Store the analyzed type in the expression
    e->analyzedType = result;
    return result;
}

void analyze_stmt(Scope* scope, FuncTable* funcTable, Stmt* s) {
    switch (s->type) {
        case VAR_DECL_S: {
            TokenType t = analyze_expr(scope, funcTable, s->as.var_decl.expr);

            if (s->as.var_decl.ownership == OWNERSHIP_OWN && s->as.var_decl.expr->type != ALLOC_E && !(s->as.var_decl.isNullable && t == NULL_LIT_T))
                stage_error(STAGE_ANALYZER, s->loc, "'own' variables must be initialized with 'alloc'");

            if (s->as.var_decl.expr->type == ALLOC_E && s->as.var_decl.ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "'alloc' can only be used with 'own' variables");

            if (t != s->as.var_decl.varType && !(s->as.var_decl.isNullable && t == NULL_LIT_T))
                stage_error(STAGE_ANALYZER, s->loc, "variable '%s' declared as %s but initialized with %s",
                      s->as.var_decl.name, token_type_name(s->as.var_decl.varType), token_type_name(t));
            declare(scope, s->as.var_decl.name, s->as.var_decl.varType, s->as.var_decl.ownership, s->as.var_decl.isNullable);
            break;
        }

        case ASSIGN_S: {
            Symbol* sym = lookup(scope, s->as.var_assign.name);
            if (sym == nullptr) {
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign to '%s', variable not declared", s->as.var_assign.name);
                break;  // Can't continue checking without symbol
            }
            s->as.var_assign.ownership = sym->ownership;
            TokenType t = analyze_expr(scope, funcTable, s->as.var_assign.expr);
            if (t != sym->type)
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign %s to '%s' of type %s",
                      token_type_name(t), s->as.var_assign.name, token_type_name(sym->type));
            if (t == VAR_T) {
                if(sym->ownership == OWNERSHIP_REF) {
                    if(s->as.var_assign.expr->as.var.ownership != OWNERSHIP_OWN)
                        stage_error(STAGE_ANALYZER, s->loc, "assigning non-own variable to '%s' not allowed!", s->as.var_assign.name);

                    sym->owner = s->as.var_assign.expr->as.var.name;
                }
            }
            break;
        }

        case IF_S: {
            TokenType c = analyze_expr(scope, funcTable, s->as.if_stmt.cond);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "if condition must be bool, got %s", token_type_name(c));

            // Check if condition is some(variable) to mark it as unwrapped in true branch
            bool isSomeCheck = false;
            char* unwrappedVarName = nullptr;
            if (s->as.if_stmt.cond->type == SOME_E &&
                s->as.if_stmt.cond->as.some.var->type == VAR_E) {
                isSomeCheck = true;
                unwrappedVarName = s->as.if_stmt.cond->as.some.var->as.var.name;
            }

            Scope* tScope = make_scope(scope);
            // Mark variable as unwrapped in the true branch (where we know it's non-null)
            if (isSomeCheck) {
                Symbol* sym = lookup(tScope, unwrappedVarName);
                if (sym) {
                    sym->is_unwrapped = true;
                }
            }
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
            declare(body, s->as.for_stmt.varName, INT_KEYWORD_T, OWNERSHIP_NONE, false);
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
            check_function_cleanup(block);
            free(block);
            break;
        }

        case EXPR_STMT_S: {
            analyze_expr(scope, funcTable, s->as.expr_stmt);
            break;
        }

        case MATCH_S: {
            // 1. Get the matched variable (special handling for nullable to bypass unwrap check)
            Symbol* matchedSym = nullptr;
            TokenType matchedType = VOID_KEYWORD_T;

            if (s->as.match_stmt.var->type == VAR_E) {
                matchedSym = lookup(scope, s->as.match_stmt.var->as.var.name);
                if (matchedSym) {
                    matchedType = matchedSym->type;
                } else {
                    stage_error(STAGE_ANALYZER, s->loc, "variable '%s' is not declared",
                               s->as.match_stmt.var->as.var.name);
                }
            } else {
                // For non-variable expressions, analyze normally
                matchedType = analyze_expr(scope, funcTable, s->as.match_stmt.var);
            }

            // 2. Determine if this is a nullable match
            bool isNullableMatch = (matchedSym && matchedSym->is_nullable);

            // 3. Validate pattern types
            bool hasSome = false, hasNull = false;

            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];

                if (branch->pattern->type == SOME_PATTERN) {
                    hasSome = true;
                    if (!isNullableMatch)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                            "some() pattern can only be used on nullable types");
                }
                else if (branch->pattern->type == NULL_PATTERN) {
                    hasNull = true;
                    if (!isNullableMatch)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                            "null pattern can only be used on nullable types");
                }
                else if (branch->pattern->type == WILDCARD_PATTERN) {
                    hasSome = hasNull = true;  // wildcard covers both
                }
            }

            // 4. Exhaustiveness check for nullable types
            if (isNullableMatch && (!hasSome || !hasNull)) {
                stage_error(STAGE_ANALYZER, s->loc,
                    "match on nullable type must handle both some and null cases");
            }

            // 5. Analyze each branch with appropriate scope
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];
                Scope* branchScope = make_scope(scope);

                // For SOME_PATTERN, declare binding variable as non-nullable reference
                if (branch->pattern->type == SOME_PATTERN && matchedSym) {
                    // Binding is always a reference (borrows from original), not a new owned variable
                    Ownership bindingOwnership = (matchedSym->ownership == OWNERSHIP_NONE)
                        ? OWNERSHIP_NONE
                        : OWNERSHIP_REF;

                    declare(branchScope,
                           branch->pattern->as.binding_name,
                           matchedSym->type,
                           bindingOwnership,
                           false);  // binding is NOT nullable

                    // Set owner for ref tracking (if it's a ref)
                    if (bindingOwnership == OWNERSHIP_REF) {
                        Symbol* bindingSym = lookup(branchScope, branch->pattern->as.binding_name);
                        if (bindingSym) {
                            bindingSym->owner = matchedSym->name;
                        }
                    }

                    // Store type for codegen
                    branch->analyzed_type = matchedSym->type;

                    // Also mark the original variable as unwrapped in this scope
                    Symbol* origSym = lookup(branchScope, matchedSym->name);
                    if (origSym) {
                        origSym->is_unwrapped = true;
                    }
                }

                // For VALUE_PATTERN, analyze the pattern expression
                if (branch->pattern->type == VALUE_PATTERN) {
                    analyze_expr(branchScope, funcTable, branch->pattern->as.value_expr);
                }

                // Analyze branch statements
                for (int j = 0; j < branch->stmtCount; j++) {
                    analyze_stmt(branchScope, funcTable, branch->stmts[j]);
                }

                // Check ownership cleanup within branch
                check_function_cleanup(branchScope);
                free(branchScope);
            }

            break;
        }

        case FREE_S: {
            Symbol* sym = lookup(scope, s->as.free_stmt.varName);
            if (sym == nullptr) {
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', variable not declared", s->as.free_stmt.varName);
                break;
            }

            // CHECK: Can only free 'own' variables
            if (sym->ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', it is not an 'own' variable", s->as.free_stmt.varName);

            // CHECK: Double free
            if (sym->state == FREED)
                stage_error(STAGE_ANALYZER, s->loc, "double free: variable '%s' has already been freed", s->as.free_stmt.varName);

            // CHECK: Free after move
            if (sym->state == MOVED)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', ownership has been moved", s->as.free_stmt.varName);

            //mark as freed
            sym->state = FREED;
            mark_dangling_refs(scope, sym->name);
            break;
        }
    }
}

void defineAndAnalyzeFunc(FuncTable* table, Func* func) {
    if(strcmp("print", func->signature->name) == 0) {
        stage_error(STAGE_ANALYZER, NO_LOC, "'print' is a reserved built-in function and cannot be redefined");
    }

    if(lookup_func_sign(table, func->signature)) {
        stage_error(STAGE_ANALYZER, NO_LOC, "Function '%s' with these parameters is already defined", func->signature->name);
    }

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
        if(check_func_sign(&t->signs[i], s))
            return &t->signs[i];
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
            declare(funcScope, fs[i]->signature->parameters[j].name, fs[i]->signature->parameters[j].type, fs[i]->signature->parameters[j].ownership, fs[i]->signature->parameters[j].isNullable);
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