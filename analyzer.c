//
// Created by bucka on 2/9/2026.
//

#include "analyzer.h"

//Forward declaration
void check_function_cleanup(Scope* scope);

//Global import registry (will be initialized in analyze_program)
static ImportRegistry* g_import_registry = nullptr;

ImportRegistry* make_import_registry() {
    ImportRegistry* reg = malloc(sizeof(ImportRegistry));
    reg->capacity = 10;
    reg->imported_functions = malloc(sizeof(char*) * reg->capacity);
    reg->count = 0;
    reg->has_wildcard_io = false;
    return reg;
}

void register_import(ImportRegistry* reg, IncludeStmt* stmt) {
    if (strcmp(stmt->module_name, "std.io") != 0) {
        stage_warning(STAGE_ANALYZER, stmt->loc, "unknown module '%s' (only std.io is supported)", stmt->module_name);
        return;
    }

    if (stmt->type == IMPORT_ALL) {
        reg->has_wildcard_io = true;
        stage_trace(STAGE_ANALYZER, "registered wildcard import: std.io.*");
    } else {
        //IMPORT_SPECIFIC
        if (reg->count >= reg->capacity) {
            reg->capacity *= 2;
            reg->imported_functions = realloc(reg->imported_functions, sizeof(char*) * reg->capacity);
        }
        reg->imported_functions[reg->count++] = stmt->function_name;
        stage_trace(STAGE_ANALYZER, "registered import: %s", stmt->function_name);
    }
}

bool is_imported(ImportRegistry* reg, const char* func_name) {
    if (reg->has_wildcard_io) {
        return true;  //All std.io functions are imported
    }

    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->imported_functions[i], func_name) == 0) {
            return true;
        }
    }
    return false;
}

Scope* make_scope(Scope* parent) {
    Scope* scope = malloc(sizeof(Scope));
    scope->capacity = 2;
    scope->symbols = malloc(sizeof(Symbol) * scope->capacity);
    scope->count = 0;
    scope->parent = parent;

    stage_trace(STAGE_ANALYZER, "created scope %p (parent=%p)", scope, parent);

    return scope;
}

void declare(Scope* scope, char* name, TokenType type, Ownership ownership, bool isNullable, bool isConst, bool isArray, int arraySize) {
    if (strcmp(name, "print") == 0 || strcmp(name, "length") == 0) {
        stage_error(STAGE_ANALYZER, NO_LOC,
                    "'%s' is a reserved built-in function and cannot be used as a variable name", name);
    }

    for (int i = 0; i < scope->count; i++) {
        if (strcmp(scope->symbols[i].name, name) == 0)
            stage_error(STAGE_ANALYZER, NO_LOC,
                        "variable '%s' already declared in this scope", name);
    }

    stage_trace(STAGE_ANALYZER, "declare %s : %s%s%s",
                name, isNullable ? "nullable " : "", isArray ? "array " : "", token_type_name(type));

    if (scope->capacity == scope->count) {
        scope->capacity *= 2;
        scope->symbols =
                realloc(scope->symbols, sizeof(Symbol) * scope->capacity);
    }

    scope->symbols[scope->count++] =
            (Symbol){
                    .type = type,
                    .name = name,
                    .ownership = ownership,
                    .is_nullable = isNullable,
                    .is_const = isConst,
                    .state = ALIVE,
                    .owner = nullptr,
                    .is_dangling = false,
                    .is_unwrapped = false,
                    .is_array = isArray,
                    .array_size = arraySize
            };
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

TokenType analyze_expr(Scope* scope, FuncTable* funcTable, Expr* e, FuncSign* currentFunc) {
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
                //Check if trying to use 'print' as a variable (give better error message)
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
            if (sym->ownership == OWNERSHIP_REF && sym->owner != nullptr) {
                Symbol* owner = lookup(scope, sym->owner);
                if (owner && owner->state != ALIVE) {
                    stage_error(STAGE_ANALYZER, e->loc, "use after owner no longer in scope: owner '%s' of '%s' is out of scope", owner->name, e->as.var.name);
                }
            }

            //Check for nullable usage in expressions (only allowed if unwrapped)
            if (sym->is_nullable && !sym->is_unwrapped) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "nullable variable '%s' must be unwrapped before use",
                            e->as.var.name);
                stage_note(STAGE_ANALYZER, e->loc,
                           "use 'match %s { some(val): { ... } null: { ... } }' to safely unwrap, or 'if(some(%s))' to check",
                           e->as.var.name, e->as.var.name);
            }

            e->as.var.ownership = sym->ownership;
            e->as.var.isConst = sym->is_const;
            result = sym->type;
            break;
        }

        case ARRAY_ACCESS_E: {
            Symbol* sym = lookup(scope, e->as.array_access.arrayName);
            if (!sym) {
                stage_error(STAGE_ANALYZER, e->loc, "undefined variable '%s'", e->as.array_access.arrayName);
                result = INT_KEYWORD_T;
                break;
            }

            if (!sym->is_array) {
                stage_error(STAGE_ANALYZER, e->loc, "'%s' is not an array", e->as.array_access.arrayName);
                result = INT_KEYWORD_T;
                break;
            }

            TokenType indexType = analyze_expr(scope, funcTable, e->as.array_access.index, currentFunc);
            if (indexType != INT_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "array index must be 'int', got '%s'", token_type_name(indexType));
            }

            result = sym->type;
            break;
        }

        case ARRAY_DECL_E: {
            if (e->as.arr_decl.count <= 0)
                stage_error(STAGE_ANALYZER, e->loc, "array cannot be initialized with %d parameters",
                            e->as.arr_decl.count);
            TokenType t = analyze_expr(scope, funcTable, e->as.arr_decl.values[0], currentFunc);
            for (int i = 0; i < e->as.arr_decl.count; ++i) {
                TokenType ta = analyze_expr(scope, funcTable, e->as.arr_decl.values[0], currentFunc);
                if (t != ta)
                    stage_error(STAGE_ANALYZER, e->loc,
                                "all parameters in array need to be the same type! Expected '%s' but got '%s'",
                                token_type_name(t), token_type_name(ta));
            }
            result = t;
            break;
        }

        case UN_OP_E: {
            TokenType operand = analyze_expr(scope, funcTable, e->as.un_op.expr, currentFunc);

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
            TokenType left = analyze_expr(scope, funcTable, e->as.bin_op.exprL, currentFunc);
            TokenType right = analyze_expr(scope, funcTable, e->as.bin_op.exprR, currentFunc);

            //arithmetic: int op int -> int
            if (op == PLUS_T || op == MINUS_T || op == STAR_T || op == SLASH_T) {
                if (left != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be int, got %s", token_type_name(op), token_type_name(left));
                if (right != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be int, got %s", token_type_name(op), token_type_name(right));
                result = INT_KEYWORD_T;
            }
                //comparison: int op int -> bool
            else if (op == LESS_T || op == MORE_T || op == LESS_EQUALS_T || op == MORE_EQUALS_T) {
                if (left != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "left side of '%s' must be int, got %s", token_type_name(op), token_type_name(left));
                if (right != INT_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "right side of '%s' must be int, got %s", token_type_name(op), token_type_name(right));
                result = BOOL_KEYWORD_T;
            }
                //equality: same type op same type -> bool
            else if (op == DOUBLE_EQUALS_T || op == NOT_EQUALS_T) {
                if (left != right)
                    stage_error(STAGE_ANALYZER, e->loc, "cannot compare %s with %s using '%s'", token_type_name(left), token_type_name(right), token_type_name(op));
                result = BOOL_KEYWORD_T;
            }
                //logical: bool op bool -> bool
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
            //Handle built-in 'print' function
            if(strcmp(e->as.func_call.name, "print") == 0) {
                //Type-check each argument
                for (int i = 0; i < e->as.func_call.count; ++i) {
                    TokenType argType = analyze_expr(scope, funcTable, e->as.func_call.params[i], currentFunc);
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

            //Handle length() built-in
            if (strcmp(e->as.func_call.name, "length") == 0) {
                if (e->as.func_call.count != 1) {
                    stage_error(STAGE_ANALYZER, e->loc, "length() takes exactly 1 argument");
                }

                Expr* arg = e->as.func_call.params[0];
                if (arg->type != VAR_E) {
                    stage_error(STAGE_ANALYZER, e->loc, "length() argument must be a variable");
                }

                Symbol* sym = lookup(scope, arg->as.var.name);
                if (!sym) {
                    stage_error(STAGE_ANALYZER, e->loc, "undefined variable '%s'", arg->as.var.name);
                } else if (!sym->is_array) {
                    stage_error(STAGE_ANALYZER, e->loc, "'%s' is not an array", arg->as.var.name);
                } else if (sym->ownership == OWNERSHIP_OWN) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "length() not supported for heap-allocated arrays");
                } else {
                    //replace function call with constant
                    e->type = INT_LIT_E;
                    e->as.int_val = sym->array_size;
                    if(sym->array_size < 0)
                        stage_error(STAGE_ANALYZER, e->loc, "dynamically sized array (not using int literal during construction) are not supported by length()");
                }

                e->as.func_call.resolved_sign = nullptr;
                result = INT_KEYWORD_T;
                break;
            }

            //Handle std.io read_* functions
            if (strcmp(e->as.func_call.name, "read_int") == 0 ||
                strcmp(e->as.func_call.name, "read_str") == 0 ||
                strcmp(e->as.func_call.name, "read_bool") == 0 ||
                strcmp(e->as.func_call.name, "read_char") == 0 ||
                strcmp(e->as.func_call.name, "read_key") == 0) {

                //Check if function is imported
                if (!is_imported(g_import_registry, e->as.func_call.name)) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "'%s' is not imported (add 'using std.io.%s;' or 'using std.io.*;')",
                                e->as.func_call.name, e->as.func_call.name);
                }

                //These functions take no arguments
                if (e->as.func_call.count != 0) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "'%s' takes no arguments", e->as.func_call.name);
                }

                //Determine return type based on function name
                if (strcmp(e->as.func_call.name, "read_int") == 0) {
                    result = INT_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_str") == 0) {
                    result = STR_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_bool") == 0) {
                    result = BOOL_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_char") == 0 ||
                           strcmp(e->as.func_call.name, "read_key") == 0) {
                    result = CHAR_KEYWORD_T;
                }

                //Mark this expression as nullable
                e->is_nullable = true;
                e->as.func_call.resolved_sign = NULL;
                break;
            }

            //Analyze all arguments first
            TokenType* argTypes = malloc(sizeof(TokenType) * e->as.func_call.count);
            for (int i = 0; i < e->as.func_call.count; ++i) {
                argTypes[i] = analyze_expr(scope, funcTable, e->as.func_call.params[i], currentFunc);
            }

            //Find ALL matching overloads by name and arity
            FuncSign** matches = malloc(sizeof(FuncSign*) * funcTable->count);
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
                free(matches);
                e->as.func_call.resolved_sign = NULL;
                result = VOID_KEYWORD_T;
                break;
            }

            //Find exact type match
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

                //Show argument types as a note
                char arg_buffer[256];
                char* ptr = arg_buffer;
                ptr += sprintf(ptr, "argument types: (");
                for (int i = 0; i < e->as.func_call.count; i++) {
                    if (i > 0) ptr += sprintf(ptr, ", ");
                    ptr += sprintf(ptr, "%s", token_type_name(argTypes[i]));
                }
                ptr += sprintf(ptr, ")");
                stage_note(STAGE_ANALYZER, e->loc, "%s", arg_buffer);

                //Show each candidate as a separate note
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
                free(matches);
                e->as.func_call.resolved_sign = NULL;
                result = VOID_KEYWORD_T;
                break;
            }

            //Store the resolved signature
            e->as.func_call.resolved_sign = match;

            //Handle ownership transfer for 'own' parameters
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
            free(matches);
            break;
        }

        case FUNC_RET_E: {
            stage_trace(STAGE_ANALYZER, "FUNC_RET_E: analyzing return expression");
            result = analyze_expr(scope, funcTable, e->as.func_ret_expr, currentFunc);
            stage_trace(STAGE_ANALYZER, "FUNC_RET_E: finished analyzing return expression");

            //Check ownership transfer on return
            if (!currentFunc) {
                stage_trace(STAGE_ANALYZER, "WARNING: currentFunc is NULL in FUNC_RET_E");
                break;
            }

            stage_trace(STAGE_ANALYZER, "FUNC_RET_E: currentFunc=%p, retOwnership=%d", currentFunc, currentFunc->retOwnership);

            if (e->as.func_ret_expr->type == VAR_E) {
                stage_trace(STAGE_ANALYZER, "FUNC_RET_E: return expr is VAR_E");
                Symbol* sym = lookup(scope, e->as.func_ret_expr->as.var.name);
                stage_trace(STAGE_ANALYZER, "FUNC_RET_E: lookup complete, sym=%p", sym);

                if (sym && sym->ownership == OWNERSHIP_OWN) {
                    stage_trace(STAGE_ANALYZER, "FUNC_RET_E: returning owned variable");
                    //Owned variables can ONLY be returned if function returns 'own'
                    if (currentFunc->retOwnership == OWNERSHIP_OWN) {
                        //Transfer ownership - mark as MOVED
                        if (sym->state != ALIVE) {
                            stage_error(STAGE_ANALYZER, e->loc,
                                        "cannot return '%s': already moved or freed", sym->name);
                        } else {
                            sym->state = MOVED;
                            stage_trace(STAGE_ANALYZER, "moved '%s' via return", sym->name);
                        }
                    } else if (currentFunc->retOwnership == OWNERSHIP_REF) {
                        stage_error(STAGE_ANALYZER, e->loc,
                                    "cannot return owned variable '%s' as 'ref' - would leak or dangle (function must return 'own' or free before returning)",
                                    sym->name);
                    } else {
                        //Returning by value
                        stage_error(STAGE_ANALYZER, e->loc,
                                    "cannot return owned variable '%s' by value - would cause memory leak (function must return 'own' to transfer ownership)",
                                    sym->name);
                    }
                }
                    //Non-owned variables
                else if (sym) {
                    if (currentFunc->retOwnership == OWNERSHIP_OWN) {
                        if (sym->ownership == OWNERSHIP_NONE) {
                            stage_error(STAGE_ANALYZER, e->loc,
                                        "cannot return non-owned variable '%s' from function returning 'own'",
                                        sym->name);
                        } else if (sym->ownership == OWNERSHIP_REF) {
                            stage_error(STAGE_ANALYZER, e->loc,
                                        "cannot return borrowed reference '%s' as 'own'",
                                        sym->name);
                        }
                    }
                    //ref/value returns are fine for non-owned variables
                }
            }
                //For alloc expressions being returned
            else if (e->as.func_ret_expr->type == ALLOC_E) {
                if (currentFunc->retOwnership != OWNERSHIP_OWN) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "cannot return 'alloc' from function that doesn't return 'own'");
                }
            }

            break;
        }

        case MATCH_E: {
            //Get the matched variable (special handling for nullable to bypass unwrap check)
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
                //For non-variable expressions, analyze normally
                targetType = analyze_expr(scope, funcTable, e->as.match.var, currentFunc);
            }

            bool isNullableMatch = (matchedSym && matchedSym->is_nullable);
            bool hasDefault = false;
            bool hasSome = false, hasNull = false;
            TokenType resultType = VOID_KEYWORD_T;

            for (int i = 0; i < e->as.match.branchCount; i++) {
                MatchBranchExpr* branch = &e->as.match.branches[i];

                //Validate pattern types
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
                    TokenType patternType = analyze_expr(scope, funcTable, branch->pattern->as.value_expr, currentFunc);
                    if (patternType != targetType)
                        stage_error(STAGE_ANALYZER, branch->pattern->loc,
                                    "match pattern type %s doesn't match target type %s",
                                    token_type_name(patternType), token_type_name(targetType));
                }

                //For SOME_PATTERN in expression match, create a temporary scope with the binding
                Scope* branchScope = scope;
                if (branch->pattern->type == SOME_PATTERN && matchedSym) {
                    branchScope = make_scope(scope);

                    //Binding is always a reference (borrows from original)
                    Ownership bindingOwnership = (matchedSym->ownership == OWNERSHIP_NONE)
                                                 ? OWNERSHIP_NONE
                                                 : OWNERSHIP_REF;

                    declare(branchScope,
                            branch->pattern->as.binding_name,
                            matchedSym->type,
                            bindingOwnership,
                            false, matchedSym->is_const, false, 0);

                    //Set owner for ref tracking
                    if (bindingOwnership == OWNERSHIP_REF) {
                        Symbol* bindingSym = lookup(branchScope, branch->pattern->as.binding_name);
                        if (bindingSym) {
                            bindingSym->owner = matchedSym->name;
                        }
                    }

                    //Store type for codegen
                    branch->analyzed_type = matchedSym->type;

                    //Mark original variable as unwrapped in this branch scope
                    Symbol* origSym = lookup(branchScope, matchedSym->name);
                    if (origSym) {
                        origSym->is_unwrapped = true;
                    }
                }

                TokenType bodyType = analyze_expr(branchScope, funcTable, branch->caseRet, currentFunc);

                //Clean up temporary scope
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
            //some() is allowed to access nullable variables without unwrapping
            //Just verify the variable exists and is nullable
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
                //If it's not a simple variable, analyze it normally
                analyze_expr(scope, funcTable, e->as.some.var, currentFunc);
            }
            result = BOOL_KEYWORD_T;
            break;
        }
        case ALLOC_E: {
            e->as.alloc.type = analyze_expr(scope, funcTable, e->as.alloc.initialValue, currentFunc);
            result =  e->as.alloc.type;
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

    //Store the analyzed type in the expression
    e->analyzedType = result;
    return result;
}

void analyze_stmt(Scope* scope, FuncTable* funcTable, Stmt* s, FuncSign* currentFunc) {
    switch (s->type) {
        case VAR_DECL_S: {
            //Skip type analysis for uninitialized arrays (VOID_E placeholder)
            TokenType t = VOID_KEYWORD_T;
            if (s->as.var_decl.expr->type != VOID_E) {
                t = analyze_expr(scope, funcTable, s->as.var_decl.expr, currentFunc);
            }

            bool is_owned_func_call = false;
            if (s->as.var_decl.expr->type == FUNC_CALL_E) {
                FuncSign* sig = s->as.var_decl.expr->as.func_call.resolved_sign;
                if (sig && sig->retOwnership == OWNERSHIP_OWN) {
                    is_owned_func_call = true;
                }
            }

            bool valid_own_init = (s->as.var_decl.expr->type == ALLOC_E) ||
                                  (s->as.var_decl.isNullable && t == NULL_LIT_T) ||
                                  (s->as.var_decl.expr->type == VOID_E) ||
                                  is_owned_func_call ||
                                  (s->as.var_decl.expr->type == VAR_E &&
                                   s->as.var_decl.expr->as.var.ownership == OWNERSHIP_OWN);

            if (s->as.var_decl.ownership == OWNERSHIP_OWN && !valid_own_init) {
                stage_error(STAGE_ANALYZER, s->loc, "'own' variables must be initialized with 'alloc' or a function returning 'own'");
            }

            //Handle move semantics: if initializing own from another own variable, mark source as MOVED
            if (s->as.var_decl.ownership == OWNERSHIP_OWN && s->as.var_decl.expr->type == VAR_E) {
                Symbol* src = lookup(scope, s->as.var_decl.expr->as.var.name);
                if (src && src->ownership == OWNERSHIP_OWN) {
                    if (src->state != ALIVE) {
                        stage_error(STAGE_ANALYZER, s->loc,
                                    "cannot move from '%s': already moved or freed", src->name);
                    } else {
                        src->state = MOVED;
                        stage_trace(STAGE_ANALYZER, "moved '%s' to '%s'", src->name, s->as.var_decl.name);
                    }
                }
            }

            if (s->as.var_decl.expr->type == ALLOC_E && s->as.var_decl.ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "'alloc' can only be used with 'own' variables");

            if (s->as.var_decl.expr->type != VOID_E && t != s->as.var_decl.varType && !(s->as.var_decl.isNullable && t == NULL_LIT_T))
                stage_error(STAGE_ANALYZER, s->loc, "variable '%s' declared as %s but initialized with %s",
                            s->as.var_decl.name, token_type_name(s->as.var_decl.varType), token_type_name(t));

            if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_NONE) {
                //Check for VLA initialization (C limitation)
                if (s->as.var_decl.arraySize != NULL &&
                    s->as.var_decl.arraySize->type != INT_LIT_E &&
                    s->as.var_decl.expr->type == ARRAY_DECL_E) {
                    stage_error(STAGE_ANALYZER, s->loc,
                                "stack arrays with variable size cannot be initialized (C language limitation)");
                    stage_note(STAGE_ANALYZER, s->loc,
                               "use a constant size like 'arr: int[5] = {...}', or use heap allocation with 'own'");
                }

                //Only require array literal for constant-sized arrays
                bool isConstantSize = (s->as.var_decl.arraySize != NULL &&
                                       s->as.var_decl.arraySize->type == INT_LIT_E);

                if(isConstantSize && s->as.var_decl.expr->type != ARRAY_DECL_E)
                    stage_error(STAGE_ANALYZER, s->loc, "stack arrays with constant size must be initialized with array literal");

                if(s->as.var_decl.expr->type == ARRAY_DECL_E) {
                    if(s->as.var_decl.varType != t)
                        stage_error(STAGE_ANALYZER, s->loc, "cannot assign array of type '%s' to array of type '%s'", token_type_name(t), token_type_name(s->as.var_decl.varType));
                    if(s->as.var_decl.arraySize != NULL && s->as.var_decl.arraySize->type == INT_LIT_E)
                        if(s->as.var_decl.arraySize->as.int_val != s->as.var_decl.expr->as.arr_decl.count)
                            stage_error(STAGE_ANALYZER, s->loc, "array length mismatch, expected %d but got %d", s->as.var_decl.arraySize->as.int_val, s->as.var_decl.expr->as.arr_decl.count);
                }
            } else if (s->as.var_decl.isArray) {
                if (!s->as.var_decl.isArray)
                    stage_error(STAGE_ANALYZER, s->loc, "heap arrays must be initialized with array");
                if (s->as.var_decl.isArray)
                    if(analyze_expr(scope, funcTable, s->as.var_decl.expr, currentFunc) != INT_KEYWORD_T)
                        stage_error(STAGE_ANALYZER, s->loc, "array initialization must be int");
            }

            int arraySize = -1;
            if (s->as.var_decl.isArray && s->as.var_decl.arraySize != NULL) {
                if (s->as.var_decl.arraySize->type == INT_LIT_E) {
                    arraySize = s->as.var_decl.arraySize->as.int_val;
                }
            }
            declare(scope, s->as.var_decl.name, s->as.var_decl.varType, s->as.var_decl.ownership, s->as.var_decl.isNullable, s->as.var_decl.isConst, s->as.var_decl.isArray, arraySize);

            // Set element ownership on the symbol
            if (s->as.var_decl.elementOwnership != OWNERSHIP_NONE) {
                Symbol* sym = lookup(scope, s->as.var_decl.name);
                if (sym) sym->element_ownership = s->as.var_decl.elementOwnership;
            }

            // For ref variables, set the owner to the variable being borrowed
            if (s->as.var_decl.ownership == OWNERSHIP_REF) {
                if (s->as.var_decl.expr->type == VAR_E) {
                    Symbol* refSym = lookup(scope, s->as.var_decl.name);
                    if (refSym && s->as.var_decl.expr->as.var.ownership == OWNERSHIP_OWN) {
                        refSym->owner = s->as.var_decl.expr->as.var.name;
                        refSym->is_const = s->as.var_decl.expr->as.var.isConst;
                    } else if (refSym) {
                        stage_error(STAGE_ANALYZER, s->loc, "ref variable '%s' can only borrow from 'own' variables", s->as.var_decl.name);
                    }
                }
            }
            break;
        }

        case ASSIGN_S: {
            stage_trace(STAGE_ANALYZER, "analyzing assignment to '%s'", s->as.var_assign.name);
            Symbol* sym = lookup(scope, s->as.var_assign.name);
            if (sym == nullptr) {
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign to '%s', variable not declared", s->as.var_assign.name);
                break;
            }
            stage_trace(STAGE_ANALYZER, "  current state: %d (0=ALIVE, 1=MOVED, 2=FREED)", sym->state);
            if (sym->is_const) {
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign to '%s', variable is immutable", s->as.var_assign.name);
                break;
            }
            if (sym->is_array && s->as.var_assign.expr->type != ALLOC_E) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "cannot assign to array '%s' directly, use element-wise assignment (arr[i] = val)",
                            s->as.var_assign.name);
                break;
            }
            s->as.var_assign.ownership = sym->ownership;
            s->as.var_assign.isArray = sym->is_array;
            s->as.var_assign.arraySize = sym->array_size;
            TokenType t = analyze_expr(scope, funcTable, s->as.var_assign.expr, currentFunc);
            if (t != sym->type)
                stage_error(STAGE_ANALYZER, s->loc, "cannot assign %s to '%s' of type %s",
                            token_type_name(t), s->as.var_assign.name, token_type_name(sym->type));

            // Allow reassigning to freed own variables with alloc
            if (sym->ownership == OWNERSHIP_OWN && sym->state == FREED && s->as.var_assign.expr->type == ALLOC_E) {
                stage_trace(STAGE_ANALYZER, "resurrecting freed variable '%s' with new alloc", sym->name);
                sym->state = ALIVE;
            }

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
            TokenType c = analyze_expr(scope, funcTable, s->as.if_stmt.cond, currentFunc);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "if condition must be bool, got %s", token_type_name(c));

            bool isSomeCheck = false;
            char* unwrappedVarName = nullptr;
            if (s->as.if_stmt.cond->type == SOME_E &&
                s->as.if_stmt.cond->as.some.var->type == VAR_E) {
                isSomeCheck = true;
                unwrappedVarName = s->as.if_stmt.cond->as.some.var->as.var.name;
            }

            Scope* tScope = make_scope(scope);
            if (isSomeCheck) {
                Symbol* sym = lookup(tScope, unwrappedVarName);
                if (sym) {
                    sym->is_unwrapped = true;
                }
            }
            analyze_stmt(tScope, funcTable, s->as.if_stmt.trueStmt, currentFunc);
            free(tScope);

            if (s->as.if_stmt.falseStmt != nullptr) {
                Scope* fScope = make_scope(scope);
                analyze_stmt(fScope, funcTable, s->as.if_stmt.falseStmt, currentFunc);
                free(fScope);
            }
            break;
        }

        case WHILE_S: {
            TokenType c = analyze_expr(scope, funcTable, s->as.while_stmt.cond, currentFunc);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "while condition must be bool, got %s", token_type_name(c));
            Scope* body = make_scope(scope);
            analyze_stmt(body, funcTable, s->as.while_stmt.body, currentFunc);
            free(body);
            break;
        }

        case DO_WHILE_S: {
            Scope* body = make_scope(scope);
            analyze_stmt(body, funcTable, s->as.do_while_stmt.body, currentFunc);
            free(body);
            TokenType c = analyze_expr(scope, funcTable, s->as.do_while_stmt.cond, currentFunc);
            if (c != BOOL_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "do-while condition must be bool, got %s", token_type_name(c));
            break;
        }

        case FOR_S: {
            Scope* body = make_scope(scope);
            declare(body, s->as.for_stmt.varName, INT_KEYWORD_T, OWNERSHIP_NONE, false, true, false, 0);
            if (analyze_expr(body, funcTable, s->as.for_stmt.min, currentFunc) != INT_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "for loop min must be int");
            if (analyze_expr(body, funcTable, s->as.for_stmt.max, currentFunc) != INT_KEYWORD_T)
                stage_error(STAGE_ANALYZER, s->loc, "for loop max must be int");
            analyze_stmt(body, funcTable, s->as.for_stmt.body, currentFunc);
            free(body);
            break;
        }

        case BLOCK_S: {
            Scope* block = make_scope(scope);
            for (int i = 0; i < s->as.block_stmt.count; ++i) {
                analyze_stmt(block, funcTable, s->as.block_stmt.stmts[i], currentFunc);
            }
            check_function_cleanup(block);
            free(block);
            break;
        }

        case EXPR_STMT_S: {
            analyze_expr(scope, funcTable, s->as.expr_stmt, currentFunc);
            break;
        }

        case MATCH_S: {
            //get the matched variable (special handling for nullable to bypass unwrap check)
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
                //for non-variable expressions, analyze normally
                matchedType = analyze_expr(scope, funcTable, s->as.match_stmt.var, currentFunc);
            }

            //determine if this is a nullable match
            bool isNullableMatch = (matchedSym && matchedSym->is_nullable);

            //validate pattern types
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

            //exhaustiveness check for nullable types
            if (isNullableMatch && (!hasSome || !hasNull)) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "match on nullable type must handle both some and null cases");
            }

            //analyze each branch with appropriate scope
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];
                Scope* branchScope = make_scope(scope);

                //for SOME_PATTERN, declare binding variable as non-nullable reference
                if (branch->pattern->type == SOME_PATTERN && matchedSym) {
                    //binding is always a reference (borrows from original), not a new owned variable
                    Ownership bindingOwnership = (matchedSym->ownership == OWNERSHIP_NONE)
                                                 ? OWNERSHIP_NONE
                                                 : OWNERSHIP_REF;

                    declare(branchScope,
                            branch->pattern->as.binding_name,
                            matchedSym->type,
                            bindingOwnership,
                            false, matchedSym->is_const, false, 0);

                    // Set owner for ref tracking (if it's a ref)
                    if (bindingOwnership == OWNERSHIP_REF) {
                        Symbol* bindingSym = lookup(branchScope, branch->pattern->as.binding_name);
                        if (bindingSym) {
                            bindingSym->owner = matchedSym->name;
                        }
                    }

                    //store type for codegen
                    branch->analyzed_type = matchedSym->type;

                    //also mark the original variable as unwrapped in this scope
                    Symbol* origSym = lookup(branchScope, matchedSym->name);
                    if (origSym) {
                        origSym->is_unwrapped = true;
                    }
                }

                //for VALUE_PATTERN, analyze the pattern expression
                if (branch->pattern->type == VALUE_PATTERN) {
                    analyze_expr(branchScope, funcTable, branch->pattern->as.value_expr, currentFunc);
                }

                //analyze branch statements
                for (int j = 0; j < branch->stmtCount; j++) {
                    analyze_stmt(branchScope, funcTable, branch->stmts[j], currentFunc);
                }

                //check ownership cleanup within branch
                check_function_cleanup(branchScope);
                free(branchScope);
            }

            break;
        }

        case ARRAY_ELEM_ASSIGN_S: {
            Symbol* sym = lookup(scope, s->as.array_elem_assign.arrayName);
            if (!sym) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "undefined variable '%s'", s->as.array_elem_assign.arrayName);
                break;
            }

            if (!sym->is_array) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "'%s' is not an array", s->as.array_elem_assign.arrayName);
                break;
            }

            if (sym->is_const) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "cannot modify const array '%s'", s->as.array_elem_assign.arrayName);
                break;
            }

            TokenType indexType = analyze_expr(scope, funcTable, s->as.array_elem_assign.index, currentFunc);
            if (indexType != INT_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "array index must be 'int', got '%s'", token_type_name(indexType));
            }

            TokenType valueType = analyze_expr(scope, funcTable, s->as.array_elem_assign.value, currentFunc);
            if (valueType != sym->type) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "cannot assign '%s' to array of type '%s'",
                            token_type_name(valueType), token_type_name(sym->type));
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

            // Set cascading free info for codegen
            s->as.free_stmt.isArrayOfOwned = (sym->is_array && sym->element_ownership == OWNERSHIP_OWN);
            s->as.free_stmt.arraySize = sym->array_size;

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
    } else if(strcmp("read_int", func->signature->name) == 0) {
        stage_error(STAGE_ANALYZER, NO_LOC, "'print' is a reserved built-in function and cannot be redefined");
    } else if(strcmp("read_str", func->signature->name) == 0) {
        stage_error(STAGE_ANALYZER, NO_LOC, "'print' is a reserved built-in function and cannot be redefined");
    } else if(strcmp("read_bool", func->signature->name) == 0) {
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

    //add to table - need deep copy of FuncSign
    stage_trace(STAGE_ANALYZER, "defineAndAnalyzeFunc: copying func '%s'", func->signature->name);
    stage_trace(STAGE_ANALYZER, "  func->signature=%p, name=%p ('%s')",
                func->signature, func->signature->name, func->signature->name);

    FuncSign copy;
    copy.name = strdup(func->signature->name);  // Make a real copy of the name string
    copy.retType = func->signature->retType;
    copy.retOwnership = func->signature->retOwnership;
    copy.paramNum = func->signature->paramNum;

    // Deep copy parameters array
    if (copy.paramNum > 0) {
        copy.parameters = malloc(sizeof(FuncParam) * copy.paramNum);
        for (int i = 0; i < copy.paramNum; i++) {
            copy.parameters[i] = func->signature->parameters[i];
            // param names also come from tokens, stay alive
        }
    } else {
        copy.parameters = NULL;
    }

    if(table->count >= table->capacity) {
        stage_error(STAGE_ANALYZER, NO_LOC, "INTERNAL ERROR: FuncTable capacity exceeded");
        return;
    }

    stage_trace(STAGE_ANALYZER, "  storing at table->signs[%d], table=%p, signs=%p",
                table->count, table, table->signs);
    stage_trace(STAGE_ANALYZER, "  address of table->signs[%d] = %p",
                table->count, &table->signs[table->count]);
    table->signs[table->count] = copy;
    stage_trace(STAGE_ANALYZER, "  stored, table->signs[%d].name=%p ('%s')",
                table->count, table->signs[table->count].name, table->signs[table->count].name);
    table->count++;
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
        Symbol* s = &scope->symbols[i];
        if (s->ownership == OWNERSHIP_OWN) {
            if (s->state == ALIVE) {
                // This is a leak!
                stage_error(STAGE_ANALYZER, NO_LOC, "Memory leak: '%s' is not freed or moved", s->name);
            }
            // If state is MOVED or FREED, we are happy.
        }
    }
}

void analyze_program(Program* prog) {
    Scope* global = make_scope(nullptr);
    FuncTable* funcTable = make_funcTable();

    // Initialize and process imports
    g_import_registry = make_import_registry();
    for (int i = 0; i < prog->imports->import_count; i++) {
        register_import(g_import_registry, prog->imports->imports[i]);
    }

    Func** fs = prog->functions;
    int count = prog->func_count;

    //pre-allocate enough space for all functions to avoid realloc invalidating pointers
    if (count > funcTable->capacity) {
        funcTable->capacity = count;
        funcTable->signs = realloc(funcTable->signs, sizeof(FuncSign) * funcTable->capacity);
    }

    for (int i = 0; i < count; ++i) {
        defineAndAnalyzeFunc(funcTable, fs[i]);
    }
    for (int i = 0; i < count; ++i) {
        Scope* funcScope = make_scope(global);

        for (int j = 0; j < fs[i]->signature->paramNum; ++j) {
            declare(funcScope, fs[i]->signature->parameters[j].name, fs[i]->signature->parameters[j].type, fs[i]->signature->parameters[j].ownership, fs[i]->signature->parameters[j].isNullable, fs[i]->signature->parameters[j].isConst, false, 0);
        }

        analyze_stmt(funcScope, funcTable, fs[i]->body, fs[i]->signature);

        check_function_cleanup(funcScope);

        free(funcScope->symbols);
        free(funcScope);
    }

    // DON'T free funcTable yet - codegen needs the resolved_sign pointers!
    // TODO: Free after codegen or store FuncTable in Program
    // free(funcTable->signs);
    // free(funcTable);

    free(global->symbols);
    free(global);
}