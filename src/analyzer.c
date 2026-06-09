//created by bucka on 2/9/2026.

#include "analyzer.h"

//forward declaration
void check_function_cleanup(Scope* scope);

// Set by analyze_program for the duration of one analysis pass so the
// function-call analyzer can queue + drain template instantiations
// during UFCS method dispatch (e.g. `xs.Push(7)` -> ListPush<int>).
static Program* g_analyzer_program = NULL;

// Implicit numeric conversions follow C#'s "widening only" rule:
//   char -> int -> float -> double
// Going wider (lower rank to higher) is implicit. Going narrower
// (double -> float, double -> int, float -> int) requires an explicit
// suffix (1.5f) or cast — the user has to acknowledge precision loss.
// This matches every game-dev's mental model from C++/C#/Unity.
static int type_rank(TokenType t) {
    switch (t) {
        case CHAR_KEYWORD_T:   return 1;
        case INT_KEYWORD_T:    return 2;
        // usize lives at rank 2 too -- same width on 64-bit platforms,
        // implicit conversion both ways. Loses sign safety in theory; in
        // practice every Lync int is positive at the call sites that care
        // (lengths, counts, sizeof).
        case USIZE_KEYWORD_T:  return 2;
        case FLOAT_KEYWORD_T:  return 3;
        case DOUBLE_KEYWORD_T: return 4;
        default:               return 0;   // not numeric
    }
}
static bool numeric_compatible(TokenType to, TokenType from) {
    if (to == from) return true;
    // Function references decay to plain pointers, mirroring C's implicit
    // function-to-pointer conversion. This is what makes
    //   Each<Velocity>(tick);
    // compile when `tick` is a `def` — the host's `Each<T>(cb: ptr)` slot
    // accepts the function reference without an explicit cast.
    if (to == PTR_KEYWORD_T && from == FN_T) return true;
    const int rt = type_rank(to);
    const int rf = type_rank(from);
    if (rt == 0 || rf == 0) return false;   // either side not numeric
    return rf <= rt;                         // widening only
}

//global import registry (will be initialized in analyze_program)
static ImportRegistry* g_import_registry = nullptr;

// Global struct table — set up at the start of analyze_program from the
// program's collected struct decls. Used by VAR_DECL_S (validate struct
// type), FIELD_ACCESS_E (resolve field type), FIELD_ASSIGN_S (validate
// LHS resolves to a struct type and that the field exists).
static StructTable* g_struct_table = nullptr;

StructTable* make_struct_table() {
    StructTable* t = malloc(sizeof(StructTable));
    t->capacity = 8;
    t->count    = 0;
    t->decls    = malloc(sizeof(StructDecl*) * t->capacity);
    return t;
}

void register_struct(StructTable* t, StructDecl* d) {
    if (lookup_struct(t, d->name)) {
        stage_error(STAGE_ANALYZER, d->loc,
                    "struct '%s' already declared", d->name);
        return;
    }
    if (t->count >= t->capacity) {
        t->capacity *= 2;
        t->decls = realloc(t->decls, sizeof(StructDecl*) * t->capacity);
    }
    t->decls[t->count++] = d;
}

StructDecl* lookup_struct(StructTable* t, const char* name) {
    if (!t || !name) return nullptr;
    for (int i = 0; i < t->count; ++i) {
        if (strcmp(t->decls[i]->name, name) == 0) return t->decls[i];
    }
    return nullptr;
}

StructField* lookup_field(StructDecl* d, const char* field_name) {
    if (!d || !field_name) return nullptr;
    for (int i = 0; i < d->field_count; ++i) {
        if (strcmp(d->fields[i].name, field_name) == 0) return &d->fields[i];
    }
    return nullptr;
}

ImportRegistry* make_import_registry() {
    ImportRegistry* reg = malloc(sizeof(ImportRegistry));
    reg->capacity = 10;
    reg->imported_functions = malloc(sizeof(char*) * reg->capacity);
    reg->count = 0;
    reg->has_wildcard_io = false;
    return reg;
}

void register_import(ImportRegistry* reg, IncludeStmt* stmt) {
    //non-std modules are handled by file_loader, just register std.io here
    if (strncmp(stmt->module_name, "std.", 4) != 0) {
        //user file import — functions are already merged by file_loader
        //just register the function names so is_imported() works
        if (stmt->type == IMPORT_ALL) {
            //for wildcard user imports, functions are already in the program
            //nothing to register here — theyre regular functions
            return;
        } else {
            //register specific import name
            if (reg->count >= reg->capacity) {
                reg->capacity *= 2;
                reg->imported_functions = realloc(reg->imported_functions, sizeof(char*) * reg->capacity);
            }
            reg->imported_functions[reg->count++] = stmt->function_name;
            return;
        }
    }

    // std.io is the inline-codegen builtin (printf/read_int/etc.). Other
    // std.* modules are loaded as regular files from the stdlib directory by
    // file_loader; their functions appear as ordinary defs by the time we
    // run, so registration here just records the wildcard for is_imported().
    if (strcmp(stmt->module_name, "std.io") != 0) {
        if (stmt->type == IMPORT_ALL) {
            stage_trace(STAGE_ANALYZER, "registered wildcard import: %s.*", stmt->module_name);
        } else {
            if (reg->count >= reg->capacity) {
                reg->capacity *= 2;
                reg->imported_functions = realloc(reg->imported_functions, sizeof(char*) * reg->capacity);
            }
            reg->imported_functions[reg->count++] = stmt->function_name;
            stage_trace(STAGE_ANALYZER, "registered import: %s.%s", stmt->module_name, stmt->function_name);
        }
        return;
    }

    if (stmt->type == IMPORT_ALL) {
        reg->has_wildcard_io = true;
        stage_trace(STAGE_ANALYZER, "registered wildcard import: std.io.*");
    } else {
        //iMPORT_SPECIFIC
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
        return true;  //all std.io functions are imported
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

    // Shadow detection (Rider/CLion-style). If `name` exists in any
    // ENCLOSING scope, emit a warning so the user can rename. Same scope
    // is rejected as a hard error above; this only fires for outer-scope
    // hits. Skips parameter names (parent == NULL means top-level which
    // we never want to warn about either).
    if (scope->parent) {
        Symbol* outer = lookup(scope->parent, name);
        if (outer) {
            stage_warning(STAGE_ANALYZER, NO_LOC,
                "'%s' shadows a name from an outer scope - consider renaming",
                name);
        }
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
                    .type_name = nullptr,   // struct callers set after declare()
                    .fn_sig = nullptr,      // fn-pointer callers set after declare()
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

        case FLOAT_LIT_E:
            result = e->analyzedType;
            break;

        case NULL_LIT_E:
            result = NULL_LIT_T;
            break;

        case VAR_E: {
            Symbol* sym = lookup(scope, e->as.var.name);
            if (sym == nullptr) {
                // Not a variable — try resolving as a function name. This
                // is how `my_func` becomes a function pointer value when
                // used in expression position (e.g. as a callback arg).
                FuncSign* fs = lookup_func_name(funcTable, e->as.var.name);
                if (fs) {
                    e->analyzed_fn_sig    = fs;
                    e->analyzed_type_name = NULL;
                    result = FN_T;
                    break;
                }
                //check if trying to use print as a variable (give better error message)
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

            //check for nullable usage in expressions (only allowed if unwrapped)
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
            // Propagate the struct's type name through analyzedType machinery
            // so chained field access (a.b.c) can resolve b's type from a.
            e->analyzed_type_name = sym->type_name;
            result = sym->type;
            break;
        }

        case FIELD_ACCESS_E: {
            // First analyze the target — could be a Var, another field
            // access (chained), etc. Result tells us what struct type the
            // target evaluates to.
            TokenType targetType = analyze_expr(scope, funcTable,
                                                 e->as.field_access.target,
                                                 currentFunc);
            if (targetType != VAR_T) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "field access requires struct type, got '%s'",
                            token_type_name(targetType));
                result = INT_KEYWORD_T;
                break;
            }
            const char* type_name =
                e->as.field_access.target->analyzed_type_name;
            StructDecl* sd = lookup_struct(g_struct_table, type_name);
            if (!sd) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "unknown struct type '%s' on field access",
                            type_name ? type_name : "(unnamed)");
                result = INT_KEYWORD_T;
                break;
            }
            StructField* fld = lookup_field(sd, e->as.field_access.field_name);
            if (!fld) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "struct '%s' has no field '%s'",
                            sd->name, e->as.field_access.field_name);
                result = INT_KEYWORD_T;
                break;
            }
            // Visibility check. Private fields are only readable from
            // inside the struct's own methods (functions whose first
            // param has matching type -- which is exactly how the
            // inline-method desugar names them).
            if (fld->is_private) {
                bool inside_method = false;
                if (currentFunc &&
                    currentFunc->paramNum > 0 &&
                    currentFunc->parameters[0].type == VAR_T &&
                    currentFunc->parameters[0].type_name &&
                    strcmp(currentFunc->parameters[0].type_name,
                           sd->name) == 0) {
                    inside_method = true;
                }
                if (!inside_method) {
                    stage_error(STAGE_ANALYZER, e->loc,
                        "field '%s' on struct '%s' is private",
                        fld->name, sd->name);
                }
            }
            e->as.field_access.field_type      = fld->type;
            e->as.field_access.field_type_name = fld->type_name;
            e->analyzed_type_name              = fld->type_name;
            // Mark target_is_ptr when the target's source is a nullable
            // VAR_T binding -- the C ABI has it as a pointer. Inside an
            // unwrap (`some(p): p.field`) the analyzer cleared
            // `is_unwrapped` but the underlying storage is still a
            // pointer, so codegen must emit `->`.
            if (e->as.field_access.target->type == VAR_E) {
                Symbol* tsym = lookup(scope,
                    e->as.field_access.target->as.var.name);
                if (tsym && tsym->type == VAR_T &&
                    (tsym->is_nullable ||
                     tsym->ownership == OWNERSHIP_OWN ||
                     tsym->ownership == OWNERSHIP_REF)) {
                    e->as.field_access.target_is_ptr = true;
                }
            } else if (e->as.field_access.target->type == FUNC_CALL_E) {
                // Field access on a function-call result: the result is a
                // pointer when the callee returns either a nullable VAR_T
                // (`def f(): T?`) or an owned/borrowed VAR_T (`def f(): own T`,
                // `def f(): ref T`). Mirrors the FIELD_ASSIGN_S branch below.
                FuncSign* rs = e->as.field_access.target->as.func_call.resolved_sign;
                if (rs && rs->retType == VAR_T &&
                    (rs->retNullable ||
                     rs->retOwnership == OWNERSHIP_OWN ||
                     rs->retOwnership == OWNERSHIP_REF)) {
                    e->as.field_access.target_is_ptr = true;
                }
            }
            result = fld->type;
            break;
        }

        case STRUCT_LIT_E: {
            // `Name { field: expr, ... }`. Validate the named struct exists,
            // every supplied field is a real field of that struct, and each
            // value's type matches the field type. Missing fields are
            // implicitly zero-initialised by the codegen path (designated
            // initializer leaves them at 0 / NULL).
            const char* tname = e->as.struct_lit.type_name;
            StructDecl* sd = lookup_struct(g_struct_table, tname);
            if (!sd) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "unknown struct type '%s' in struct literal",
                            tname ? tname : "(unnamed)");
                result = VOID_KEYWORD_T;
                break;
            }
            for (int fi = 0; fi < e->as.struct_lit.field_count; ++fi) {
                const char* fname = e->as.struct_lit.field_names[fi];
                StructField* fld  = lookup_field(sd, fname);
                if (!fld) {
                    stage_error(STAGE_ANALYZER, e->as.struct_lit.field_values[fi]->loc,
                                "struct '%s' has no field '%s'",
                                sd->name, fname);
                    continue;
                }
                TokenType vt = analyze_expr(scope, funcTable,
                                             e->as.struct_lit.field_values[fi],
                                             currentFunc);
                // Same loose-int-to-float / int-to-char relax the analyzer
                // applies elsewhere. Strict mismatch is an error; widening
                // is silent.
                if (vt != fld->type &&
                    !(fld->type == FLOAT_KEYWORD_T  && vt == INT_KEYWORD_T) &&
                    !(fld->type == DOUBLE_KEYWORD_T && vt == INT_KEYWORD_T) &&
                    !(fld->type == DOUBLE_KEYWORD_T && vt == FLOAT_KEYWORD_T) &&
                    !(fld->type == CHAR_KEYWORD_T   && vt == INT_KEYWORD_T)) {
                    stage_error(STAGE_ANALYZER,
                                e->as.struct_lit.field_values[fi]->loc,
                                "field '%s.%s' expects '%s', got '%s'",
                                sd->name, fname,
                                token_type_name(fld->type),
                                token_type_name(vt));
                }
            }
            e->analyzed_type_name = sd->name;
            result = VAR_T;
            break;
        }

        case ARRAY_ACCESS_E: {
            Symbol* sym = lookup(scope, e->as.array_access.arrayName);
            if (!sym) {
                stage_error(STAGE_ANALYZER, e->loc, "undefined variable '%s'", e->as.array_access.arrayName);
                result = INT_KEYWORD_T;
                break;
            }

            if (!sym->is_array && sym->type != STR_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, e->loc, "'%s' is not an array or string", e->as.array_access.arrayName);
                result = INT_KEYWORD_T;
                break;
            }

            TokenType indexType = analyze_expr(scope, funcTable, e->as.array_access.index, currentFunc);
            if (indexType != INT_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "array/string index must be 'int', got '%s'", token_type_name(indexType));
            }

            if (sym->type == STR_KEYWORD_T) {
                result = CHAR_KEYWORD_T;
            } else {
                result = sym->type;
            }
            break;
        }

        case ARRAY_DECL_E: {
            //... (rest of ARRAY_DECL_E remains unchanged)
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
            //... (rest of UN_OP_E remains unchanged)
            TokenType operand = analyze_expr(scope, funcTable, e->as.un_op.expr, currentFunc);

            // Unary op overload: `def -()` or `def !()` on the operand's
            // struct type. Same rewrite trick as binary -- transform the
            // UN_OP into a FUNC_CALL of `<TypeName>op_neg(x)` /
            // `<TypeName>op_not(x)`. Codegen sees a regular call.
            if (operand == VAR_T && e->as.un_op.expr->analyzed_type_name) {
                const char* opname = NULL;
                if (e->as.un_op.op == MINUS_T)         opname = "op_neg";
                else if (e->as.un_op.op == NEGATION_T) opname = "op_not";
                if (opname) {
                    char fn_name[256];
                    snprintf(fn_name, sizeof(fn_name), "%s%s",
                             e->as.un_op.expr->analyzed_type_name, opname);
                    FuncSign* matched = NULL;
                    for (int i = 0; i < funcTable->count; ++i) {
                        if (strcmp(funcTable->signs[i].name, fn_name) == 0 &&
                            funcTable->signs[i].paramNum == 1) {
                            matched = &funcTable->signs[i];
                            break;
                        }
                    }
                    if (matched) {
                        Expr* arg = e->as.un_op.expr;
                        e->type = FUNC_CALL_E;
                        e->as.func_call.name          = strdup(fn_name);
                        e->as.func_call.params        = malloc(sizeof(Expr*));
                        e->as.func_call.params[0]     = arg;
                        e->as.func_call.count         = 1;
                        e->as.func_call.resolved_sign = matched;
                        e->analyzedType               = matched->retType;
                        e->analyzed_type_name         = matched->retTypeName;
                        result = matched->retType;
                        break;
                    }
                }
            }

            if (e->as.un_op.op == MINUS_T) {
                if (operand != INT_KEYWORD_T && operand != FLOAT_KEYWORD_T && operand != DOUBLE_KEYWORD_T)
                    stage_error(STAGE_ANALYZER, e->loc, "unary '-' requires numeric type, got %s", token_type_name(operand));
                result = operand;
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

            // Pre-flight for `x == null` / `null == x` / `x != null`:
            // skip the "must be unwrapped" guard on a nullable VAR on
            // the side that's being compared to null. The whole point
            // of the comparison IS to test the null-state, so unwrap
            // isn't required. We do this by temporarily marking the
            // symbol as unwrapped for the analyze_expr call, restoring
            // right after.
            Symbol* unmark = NULL;
            bool prev_unwrapped = false;
            if ((op == DOUBLE_EQUALS_T || op == NOT_EQUALS_T) &&
                ((e->as.bin_op.exprL->type == NULL_LIT_E ||
                  e->as.bin_op.exprR->type == NULL_LIT_E))) {
                Expr* nv = (e->as.bin_op.exprL->type == NULL_LIT_E)
                    ? e->as.bin_op.exprR : e->as.bin_op.exprL;
                if (nv->type == VAR_E) {
                    Symbol* s = lookup(scope, nv->as.var.name);
                    if (s && s->is_nullable && !s->is_unwrapped) {
                        unmark = s;
                        prev_unwrapped = s->is_unwrapped;
                        s->is_unwrapped = true;
                    }
                }
            }

            TokenType left = analyze_expr(scope, funcTable, e->as.bin_op.exprL, currentFunc);
            TokenType right = analyze_expr(scope, funcTable, e->as.bin_op.exprR, currentFunc);
            if (unmark) unmark->is_unwrapped = prev_unwrapped;

            // Operator overload dispatch. When both operands are the same
            // user struct type AND the operator has a matching method
            // declared via `def +(other: T): R { ... }`, transform the
            // BIN_OP node in place into a FUNC_CALL_E that calls the
            // mangled `<TypeName>op_<name>` function. Subsequent stages
            // see a regular function call and emit it like any other.
            if (left == VAR_T && right == VAR_T &&
                e->as.bin_op.exprL->analyzed_type_name &&
                e->as.bin_op.exprR->analyzed_type_name &&
                strcmp(e->as.bin_op.exprL->analyzed_type_name,
                       e->as.bin_op.exprR->analyzed_type_name) == 0)
            {
                const char* opname = NULL;
                switch (op) {
                    case PLUS_T:          opname = "op_add"; break;
                    case MINUS_T:         opname = "op_sub"; break;
                    case STAR_T:          opname = "op_mul"; break;
                    case SLASH_T:         opname = "op_div"; break;
                    case PERCENT_T:       opname = "op_mod"; break;
                    case DOUBLE_EQUALS_T: opname = "op_eq";  break;
                    case NOT_EQUALS_T:    opname = "op_ne";  break;
                    case LESS_T:          opname = "op_lt";  break;
                    case MORE_T:          opname = "op_gt";  break;
                    case LESS_EQUALS_T:   opname = "op_le";  break;
                    case MORE_EQUALS_T:   opname = "op_ge";  break;
                    default: opname = NULL;
                }
                if (opname) {
                    char fn_name[256];
                    snprintf(fn_name, sizeof(fn_name), "%s%s",
                             e->as.bin_op.exprL->analyzed_type_name, opname);
                    FuncSign* matched = NULL;
                    for (int i = 0; i < funcTable->count; ++i) {
                        if (strcmp(funcTable->signs[i].name, fn_name) == 0 &&
                            funcTable->signs[i].paramNum == 2) {
                            matched = &funcTable->signs[i];
                            break;
                        }
                    }
                    if (matched) {
                        // Rewrite the bin_op node into a func_call.
                        Expr* lhs = e->as.bin_op.exprL;
                        Expr* rhs = e->as.bin_op.exprR;
                        e->type = FUNC_CALL_E;
                        e->as.func_call.name          = strdup(fn_name);
                        e->as.func_call.params        = malloc(sizeof(Expr*) * 2);
                        e->as.func_call.params[0]     = lhs;
                        e->as.func_call.params[1]     = rhs;
                        e->as.func_call.count         = 2;
                        e->as.func_call.resolved_sign = matched;
                        e->analyzedType               = matched->retType;
                        e->analyzed_type_name         = matched->retTypeName;
                        result = matched->retType;
                        break;   // out of the BIN_OP_E switch case
                    }
                }
            }

            //arithmetic: int/usize/char/float/double, plus pointer arithmetic
            //(ptr +/- int) used by stdlib container code that walks raw heap
            //buffers (std.list / std.string).
            if (op == PLUS_T || op == MINUS_T || op == STAR_T || op == SLASH_T || op == PERCENT_T) {
                bool isNumL = (left == INT_KEYWORD_T || left == USIZE_KEYWORD_T || left == CHAR_KEYWORD_T || left == FLOAT_KEYWORD_T || left == DOUBLE_KEYWORD_T);
                bool isNumR = (right == INT_KEYWORD_T || right == USIZE_KEYWORD_T || right == CHAR_KEYWORD_T || right == FLOAT_KEYWORD_T || right == DOUBLE_KEYWORD_T);

                // Pointer arithmetic short-circuit: ptr + int / int + ptr / ptr - int / ptr - ptr.
                // The C backend handles these natively, we just have to allow them through here.
                bool isPtrL = (left == PTR_KEYWORD_T || left == STR_KEYWORD_T);
                bool isPtrR = (right == PTR_KEYWORD_T || right == STR_KEYWORD_T);
                if ((op == PLUS_T || op == MINUS_T) && (isPtrL || isPtrR)) {
                    if (op == PLUS_T && isPtrL && isNumR)        { result = left;  break; }
                    if (op == PLUS_T && isPtrR && isNumL)        { result = right; break; }
                    if (op == MINUS_T && isPtrL && isNumR)       { result = left;  break; }
                    if (op == MINUS_T && isPtrL && isPtrR)       { result = INT_KEYWORD_T; break; }
                    stage_error(STAGE_ANALYZER, e->loc, "invalid pointer arithmetic: %s %s %s",
                                token_type_name(left), token_type_name(op), token_type_name(right));
                    result = PTR_KEYWORD_T;
                    break;
                }

                if (!isNumL || !isNumR) {
                     stage_error(STAGE_ANALYZER, e->loc, "operands of '%s' must be numeric, got %s and %s", token_type_name(op), token_type_name(left), token_type_name(right));
                     result = INT_KEYWORD_T;
                } else {
                    //promotion rules
                    if (left == DOUBLE_KEYWORD_T || right == DOUBLE_KEYWORD_T) {
                        result = DOUBLE_KEYWORD_T;
                    } else if (left == FLOAT_KEYWORD_T || right == FLOAT_KEYWORD_T) {
                        result = FLOAT_KEYWORD_T;
                    } else if (left == USIZE_KEYWORD_T || right == USIZE_KEYWORD_T) {
                        // usize "wins" over int for arithmetic on counts/sizes
                        // -- otherwise `cap * elem_size` (usize * int) demotes
                        // back to int and re-introduces the size_t conflict.
                        result = USIZE_KEYWORD_T;
                    } else {
                        result = INT_KEYWORD_T; //char promotes to int
                    }
                }
            }
                //comparison: numeric -> bool
            else if (op == LESS_T || op == MORE_T || op == LESS_EQUALS_T || op == MORE_EQUALS_T) {
                bool isNumL = (left == INT_KEYWORD_T || left == USIZE_KEYWORD_T || left == CHAR_KEYWORD_T || left == FLOAT_KEYWORD_T || left == DOUBLE_KEYWORD_T);
                bool isNumR = (right == INT_KEYWORD_T || right == USIZE_KEYWORD_T || right == CHAR_KEYWORD_T || right == FLOAT_KEYWORD_T || right == DOUBLE_KEYWORD_T);

                if (!isNumL || !isNumR) {
                    stage_error(STAGE_ANALYZER, e->loc, "operands of '%s' must be numeric, got %s and %s", token_type_name(op), token_type_name(left), token_type_name(right));
                }
                result = BOOL_KEYWORD_T;
            }
                //equality: same type or numeric -> bool
            else if (op == DOUBLE_EQUALS_T || op == NOT_EQUALS_T) {
                bool isNumL = (left == INT_KEYWORD_T || left == USIZE_KEYWORD_T || left == CHAR_KEYWORD_T || left == FLOAT_KEYWORD_T || left == DOUBLE_KEYWORD_T);
                bool isNumR = (right == INT_KEYWORD_T || right == USIZE_KEYWORD_T || right == CHAR_KEYWORD_T || right == FLOAT_KEYWORD_T || right == DOUBLE_KEYWORD_T);

                // Allow `ptr == null` / `ptr != null`. The literal `null`
                // analyzes as NULL_LIT_T; in C this compiles to `p == NULL`
                // which is the canonical null-pointer check.
                bool isPtrL  = (left  == PTR_KEYWORD_T || left  == STR_KEYWORD_T);
                bool isPtrR  = (right == PTR_KEYWORD_T || right == STR_KEYWORD_T);
                bool isNullL = (left  == NULL_LIT_T);
                bool isNullR = (right == NULL_LIT_T);
                bool ptr_null_compare =
                    (isPtrL && isNullR) || (isNullL && isPtrR) ||
                    (isPtrL && isPtrR);
                if (ptr_null_compare) {
                    result = BOOL_KEYWORD_T;
                    break;
                }

                if (left != right) {
                    if (!(isNumL && isNumR)) {
                         stage_error(STAGE_ANALYZER, e->loc, "cannot compare %s with %s using '%s'", token_type_name(left), token_type_name(right), token_type_name(op));
                    }
                    //if both numeric, we allow it (implicit promotion during codegen/backend)
                }
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
            // Static-method dispatch. The parser rewrote `Vec2.foo(x)`
            // as `foo(Vec2, x)` (UFCS unconditionally injects the
            // receiver). When the receiver is actually a *type name*
            // (a registered struct) AND a matching static method
            // `<TypeName><FnName>` exists, drop the bogus receiver and
            // re-route the call to the static function. Same dispatch
            // model the binary/unary op rewrites use; no analyzer
            // surprises downstream.
            if (e->as.func_call.count >= 1 &&
                e->as.func_call.params[0]->type == VAR_E)
            {
                const char* recv_name = e->as.func_call.params[0]->as.var.name;
                if (recv_name && lookup_struct(g_struct_table, recv_name)) {
                    char fn_name[256];
                    snprintf(fn_name, sizeof(fn_name), "%s%s",
                             recv_name, e->as.func_call.name);
                    FuncSign* matched = NULL;
                    for (int i = 0; i < funcTable->count; ++i) {
                        FuncSign* c = &funcTable->signs[i];
                        if (c->is_static &&
                            strcmp(c->name, fn_name) == 0 &&
                            c->paramNum == e->as.func_call.count - 1) {
                            matched = c;
                            break;
                        }
                    }
                    if (matched) {
                        // Drop the receiver from the args list and
                        // rename the call. Subsequent analysis sees
                        // a regular static call with the right arity.
                        for (int i = 1; i < e->as.func_call.count; ++i)
                            e->as.func_call.params[i - 1] =
                                e->as.func_call.params[i];
                        --e->as.func_call.count;
                        free(e->as.func_call.name);
                        e->as.func_call.name = strdup(fn_name);
                    }
                }
            }

            //handle built-in print function
            if(strcmp(e->as.func_call.name, "print") == 0) {
                //...
               for (int i = 0; i < e->as.func_call.count; ++i) {
                    TokenType argType = analyze_expr(scope, funcTable, e->as.func_call.params[i], currentFunc);
                    if (argType != INT_KEYWORD_T && argType != BOOL_KEYWORD_T && argType != STR_KEYWORD_T && argType != CHAR_KEYWORD_T && argType != FLOAT_KEYWORD_T && argType != DOUBLE_KEYWORD_T) {
                        stage_error(STAGE_ANALYZER, e->as.func_call.params[i]->loc,
                                    "print only supports int, bool, char, string, float, and double, got %s",
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

            //handle length() built-in
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
                } else if (sym->type == STR_KEYWORD_T) {
                    //string length support
                    result = INT_KEYWORD_T;
                    //leave as FUNC_CALL_E so codegen handles it as strlen
                } else if (!sym->is_array) {
                    stage_error(STAGE_ANALYZER, e->loc, "'%s' is not an array or string", arg->as.var.name);
                } else if (sym->ownership == OWNERSHIP_OWN) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "length() not supported for heap-allocated arrays");
                } else {
                    //replace function call with constant for arrays
                    e->type = INT_LIT_E;
                    e->as.int_val = sym->array_size;
                    if(sym->array_size < 0)
                        stage_error(STAGE_ANALYZER, e->loc, "dynamically sized array (not using int literal during construction) are not supported by length()");
                }

                e->as.func_call.resolved_sign = nullptr;
                result = INT_KEYWORD_T;
                break;
            }

            //handle std.io read_* functions
            if (strcmp(e->as.func_call.name, "read_int") == 0 ||
                strcmp(e->as.func_call.name, "read_str") == 0 ||
                strcmp(e->as.func_call.name, "read_bool") == 0 ||
                strcmp(e->as.func_call.name, "read_char") == 0 ||
                strcmp(e->as.func_call.name, "read_float") == 0 ||
                strcmp(e->as.func_call.name, "read_double") == 0 ||
                strcmp(e->as.func_call.name, "read_key") == 0) {

                //check if function is imported
                if (!is_imported(g_import_registry, e->as.func_call.name)) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "'%s' is not imported (add 'using std.io.%s;' or 'using std.io.*;')",
                                e->as.func_call.name, e->as.func_call.name);
                }

                //these functions take no arguments
                if (e->as.func_call.count != 0) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "'%s' takes no arguments", e->as.func_call.name);
                }

                //determine return type based on function name
                if (strcmp(e->as.func_call.name, "read_int") == 0) {
                    result = INT_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_str") == 0) {
                    result = STR_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_bool") == 0) {
                    result = BOOL_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_char") == 0 ||
                           strcmp(e->as.func_call.name, "read_key") == 0) {
                    result = CHAR_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_float") == 0) {
                    result = FLOAT_KEYWORD_T;
                } else if (strcmp(e->as.func_call.name, "read_double") == 0) {
                    result = DOUBLE_KEYWORD_T;
                }

                //mark this expression as nullable
                e->is_nullable = true;
                
                //create a dummy signature to handle ownership
                //we need this so that assigning to 'own' variables works
                FuncSign* sig = malloc(sizeof(FuncSign));
                sig->name = strdup(e->as.func_call.name);
                sig->retType = result;
                sig->retOwnership = OWNERSHIP_OWN; //all read_* functions return owned pointers
                sig->paramNum = 0;
                sig->parameters = NULL;
                
                e->as.func_call.resolved_sign = sig;
                break;
            }

            //analyze all arguments first
            TokenType* argTypes = malloc(sizeof(TokenType) * e->as.func_call.count);
            for (int i = 0; i < e->as.func_call.count; ++i) {
                argTypes[i] = analyze_expr(scope, funcTable, e->as.func_call.params[i], currentFunc);
            }

            //find ALL matching overloads by name and arity
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
                // No global function matched. Try resolving as a fn-typed
                // local variable (call through function pointer).
                Symbol* fp_sym = lookup(scope, e->as.func_call.name);
                if (fp_sym && fp_sym->type == FN_T && fp_sym->fn_sig &&
                    fp_sym->fn_sig->paramNum == e->as.func_call.count) {
                    e->as.func_call.resolved_sign = fp_sym->fn_sig;
                    result = fp_sym->fn_sig->retType;
                    free(argTypes);
                    free(matches);
                    break;
                }

                // UFCS method-dispatch fallback. The parser already
                // rewrote `xs.Push(7)` to `Push(xs, 7)`. If the first
                // arg is a struct type S (templated or not), retry the
                // lookup with name "<S><MethodName>" so plain
                // `xs.Push(...)` finds `ListPush` automatically -- as
                // long as the requested instantiation is already in the
                // funcTable (the parser pre-queued it via the receiver's
                // type-args at decl time, so e.g. xs's type instantiates
                // to List__int and ListPush__int rides along). Cases
                // that need a fresh instantiation discovered mid-analyze
                // still require the explicit `xs.ListPush<int>(7)` form
                // -- mid-analyze template drain is a deeper rework.
                if (e->as.func_call.count >= 1) {
                    Expr* recv = e->as.func_call.params[0];
                    if (recv->analyzedType == VAR_T &&
                        recv->analyzed_type_name) {
                        const char* tn = recv->analyzed_type_name;
                        const char* uu = strstr(tn, "__");
                        char struct_base[128];
                        int  bn = uu ? (int)(uu - tn) : (int)strlen(tn);
                        if (bn > (int)sizeof(struct_base) - 1)
                            bn = sizeof(struct_base) - 1;
                        memcpy(struct_base, tn, bn);
                        struct_base[bn] = 0;

                        // Two candidate names:
                        //   <Base><Method>           -- non-templated
                        //                               or already-mangled
                        //   <Base><Method><__args>   -- templated method
                        //                               instantiation
                        char cand[256];
                        snprintf(cand, sizeof(cand), "%s%s",
                                 struct_base, e->as.func_call.name);
                        char cand_mangled[256];
                        if (uu) {
                            snprintf(cand_mangled, sizeof(cand_mangled),
                                     "%s%s%s", struct_base,
                                     e->as.func_call.name, uu);
                        } else {
                            cand_mangled[0] = 0;
                        }
                        for (int i = 0; i < funcTable->count; i++) {
                            FuncSign* c = &funcTable->signs[i];
                            if (c->paramNum != e->as.func_call.count) continue;
                            if (strcmp(c->name, cand) == 0 ||
                                (cand_mangled[0] && strcmp(c->name, cand_mangled) == 0)) {
                                matches[matchCount++] = c;
                            }
                        }
                        if (matchCount > 0) {
                            // Rewrite call name to the resolved symbol.
                            free(e->as.func_call.name);
                            e->as.func_call.name =
                                strdup(matches[0]->name);
                        }
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
            }

            //find best type match. Exact wins; numeric-coercible accepts.
            //First pass: exact match. Second pass: implicit numeric coercion.
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
                if (typesMatch) { match = candidate; break; }
            }
            if (match == NULL) {
                for (int i = 0; i < matchCount; i++) {
                    FuncSign* candidate = matches[i];
                    bool typesMatch = true;
                    for (int j = 0; j < candidate->paramNum; j++) {
                        if (!numeric_compatible(candidate->parameters[j].type, argTypes[j])) {
                            typesMatch = false;
                            break;
                        }
                    }
                    if (typesMatch) { match = candidate; break; }
                }
            }

            if (match == NULL) {
                stage_error(STAGE_ANALYZER, e->loc,
                            "no matching overload for '%s'", e->as.func_call.name);

                //show argument types as a note
                char arg_buffer[256];
                char* ptr = arg_buffer;
                ptr += sprintf(ptr, "argument types: (");
                for (int i = 0; i < e->as.func_call.count; i++) {
                    if (i > 0) ptr += sprintf(ptr, ", ");
                    ptr += sprintf(ptr, "%s", token_type_name(argTypes[i]));
                }
                ptr += sprintf(ptr, ")");
                stage_note(STAGE_ANALYZER, e->loc, "%s", arg_buffer);

                //show each candidate as a separate note
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

            //store the resolved signature
            e->as.func_call.resolved_sign = match;

            // Visibility check. Private inline methods can only be
            // invoked from inside another method of the same struct
            // (i.e. the caller's first param has matching type, the
            // exact rule we already use for private fields).
            if (match->is_private && match->owner_struct) {
                bool inside_method = false;
                if (currentFunc &&
                    currentFunc->paramNum > 0 &&
                    currentFunc->parameters[0].type == VAR_T &&
                    currentFunc->parameters[0].type_name &&
                    strcmp(currentFunc->parameters[0].type_name,
                           match->owner_struct) == 0) {
                    inside_method = true;
                }
                if (!inside_method) {
                    stage_error(STAGE_ANALYZER, e->loc,
                        "method '%s' on struct '%s' is private",
                        match->name, match->owner_struct);
                }
            }

            //handle ownership transfer for own parameters
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
            //extern functions can be marked as returning a nullable value
            //(e.g. zues plugin's GetX(e): ptr?). Surface that on the call expr
            //so var-decl + match analysis can enforce/handle it.
            if (match->retNullable) {
                e->is_nullable = true;
            }
            // Propagate the struct return type name onto the call expr so
            // chained field access (`KeyCode().W`, `e.Get<T>().field`) can
            // resolve the LHS type. Without this the analyzer reports
            // "unknown struct type '(unnamed)' on field access" because
            // FIELD_ACCESS_E reads `target->analyzed_type_name`.
            if (match->retType == VAR_T && match->retTypeName) {
                e->analyzed_type_name = match->retTypeName;
            }
            free(argTypes);
            free(matches);
            break;
        }

        case FUNC_RET_E: {
            stage_trace(STAGE_ANALYZER, "FUNC_RET_E: analyzing return expression");
            result = analyze_expr(scope, funcTable, e->as.func_ret_expr, currentFunc);
            stage_trace(STAGE_ANALYZER, "FUNC_RET_E: finished analyzing return expression");

            //check ownership transfer on return
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
                    //owned variables can ONLY be returned if function returns own
                    if (currentFunc->retOwnership == OWNERSHIP_OWN) {
                        //transfer ownership - mark as MOVED
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
                        //returning by value
                        stage_error(STAGE_ANALYZER, e->loc,
                                    "cannot return owned variable '%s' by value - would cause memory leak (function must return 'own' to transfer ownership)",
                                    sym->name);
                    }
                }
                    //non-owned variables
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
                //for alloc expressions being returned
            else if (e->as.func_ret_expr->type == ALLOC_E) {
                if (currentFunc->retOwnership != OWNERSHIP_OWN) {
                    stage_error(STAGE_ANALYZER, e->loc,
                                "cannot return 'alloc' from function that doesn't return 'own'");
                }
            }

            break;
        }

        case MATCH_E: {
            //get the matched variable (special handling for nullable to bypass unwrap check)
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
                //for non-variable expressions, analyze normally
                targetType = analyze_expr(scope, funcTable, e->as.match.var, currentFunc);
            }

            // Same dual-source nullability check as MATCH_S: bare-var
            // path reads the symbol's flag; expression path (nullable
            // function call etc.) reads the analyzed expr's flag.
            bool isNullableMatch =
                (matchedSym && matchedSym->is_nullable) ||
                e->as.match.var->is_nullable;
            bool hasDefault = false;
            bool hasSome = false, hasNull = false;
            TokenType resultType = VOID_KEYWORD_T;

            for (int i = 0; i < e->as.match.branchCount; i++) {
                MatchBranchExpr* branch = &e->as.match.branches[i];

                //validate pattern types
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

                //for SOME_PATTERN in expression match, create a temporary scope with the binding
                Scope* branchScope = scope;
                if (branch->pattern->type == SOME_PATTERN && matchedSym) {
                    branchScope = make_scope(scope);

                    //binding is always a reference (borrows from original)
                    Ownership bindingOwnership = (matchedSym->ownership == OWNERSHIP_NONE)
                                                 ? OWNERSHIP_NONE
                                                 : OWNERSHIP_REF;

                    declare(branchScope,
                            branch->pattern->as.binding_name,
                            matchedSym->type,
                            bindingOwnership,
                            false, matchedSym->is_const, false, 0);

                    //set owner for ref tracking
                    if (bindingOwnership == OWNERSHIP_REF) {
                        Symbol* bindingSym = lookup(branchScope, branch->pattern->as.binding_name);
                        if (bindingSym) {
                            bindingSym->owner = matchedSym->name;
                        }
                    }

                    //store type for codegen
                    branch->analyzed_type = matchedSym->type;

                    //mark original variable as unwrapped in this branch scope
                    Symbol* origSym = lookup(branchScope, matchedSym->name);
                    if (origSym) {
                        origSym->is_unwrapped = true;
                    }
                }
                else if (branch->pattern->type == SOME_PATTERN && !matchedSym &&
                         e->as.match.var->is_nullable &&
                         e->as.match.var->analyzed_type_name) {
                    // Expression-match against a nullable function call
                    // (e.g. `match e.Get<T>() { some(t): t.field }`).
                    // No source Symbol to consult; pull the type from
                    // the analyzed expression and declare the binding
                    // as a non-nullable typed pointer.
                    branchScope = make_scope(scope);
                    declare(branchScope,
                            branch->pattern->as.binding_name,
                            VAR_T,
                            OWNERSHIP_NONE,
                            false, false, false, 0);
                    Symbol* bindingSym =
                        lookup(branchScope, branch->pattern->as.binding_name);
                    if (bindingSym) {
                        bindingSym->type_name =
                            e->as.match.var->analyzed_type_name;
                        bindingSym->is_nullable  = true;
                        bindingSym->is_unwrapped = true;
                    }
                    branch->analyzed_type = VAR_T;
                }

                TokenType bodyType = analyze_expr(branchScope, funcTable, branch->caseRet, currentFunc);

                //clean up temporary scope
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
            //just verify the variable exists and is nullable
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
                //if its not a simple variable, analyze it normally
                analyze_expr(scope, funcTable, e->as.some.var, currentFunc);
            }
            result = BOOL_KEYWORD_T;
            break;
        }
        case ALLOC_E: {
            TokenType initType = analyze_expr(scope, funcTable, e->as.alloc.initialValue, currentFunc);
            
            if (e->as.alloc.isArray) {
                //for array alloc (alloc[N] T), initialValue is the size (must be int)
                if (initType != INT_KEYWORD_T) {
                     stage_error(STAGE_ANALYZER, e->loc, "array allocation size must be an integer");
                }
                //the Type of the expression is the allocated type (e.g. char for alloc[n] char)
                //which was stored by the parser in e->as.alloc.type
                result = e->as.alloc.type;
            } else {
                //for single alloc (alloc expr), the type is inferred from the expression
                e->as.alloc.type = initType;
                result = initType;
            }
            break;
        }
        case CHAR_LIT_E:
            result = CHAR_KEYWORD_T;
            break;
        case VOID_E:
            result = VOID_KEYWORD_T;
            break;

        case SIZEOF_E:
            // sizeof always yields a usize; the operand type is opaque to
            // the analyzer (any type is a valid argument). Codegen handles
            // emit details.
            result = USIZE_KEYWORD_T;
            break;

        case ADDR_OF_E: {
            // Recurse on the inner expression so any of its analyzers run
            // (e.g. resolving VAR_E). The resulting type is `ptr` -- the
            // caller hands it to memcpy or stores it in an `own ptr`.
            (void)analyze_expr(scope, funcTable, e->as.addr_of.target, currentFunc);
            result = PTR_KEYWORD_T;
            break;
        }

        default:
            stage_error(STAGE_ANALYZER, e->loc, "unknown expression type %d", e->type);
            result = INT_KEYWORD_T;
            break;
    }


    e->analyzedType = result;
    return result;
}

void analyze_stmt(Scope* scope, FuncTable* funcTable, Stmt* s, FuncSign* currentFunc) {
    switch (s->type) {
        case VAR_DECL_S: {
            // Function-pointer typed declaration: analyze initializer,
            // verify it's a function reference (FN_T result with attached
            // sig), shallow-validate signature compatibility (param count
            // + return type for v1; full type-tuple match next pass).
            if (s->as.var_decl.varType == FN_T) {
                TokenType it = analyze_expr(scope, funcTable,
                                             s->as.var_decl.expr, currentFunc);
                FuncSign* init_sig = s->as.var_decl.expr->analyzed_fn_sig;
                if (it != FN_T || !init_sig) {
                    stage_error(STAGE_ANALYZER, s->loc,
                                "function-pointer '%s' must be initialized with a function name",
                                s->as.var_decl.name);
                } else {
                    FuncSign* expected = s->as.var_decl.fnSig;
                    if (expected->paramNum != init_sig->paramNum ||
                        expected->retType  != init_sig->retType) {
                        stage_error(STAGE_ANALYZER, s->loc,
                                    "function '%s' signature does not match declared fn type for '%s'",
                                    init_sig->name ? init_sig->name : "(?)",
                                    s->as.var_decl.name);
                    }
                }
                declare(scope, s->as.var_decl.name, FN_T,
                        OWNERSHIP_NONE, false, s->as.var_decl.isConst, false, 0);
                Symbol* sym = lookup(scope, s->as.var_decl.name);
                if (sym) sym->fn_sig = s->as.var_decl.fnSig;
                break;
            }

            // Struct-typed declaration: validate the type name resolves to
            // a registered struct, declare the symbol, and stamp its
            // type_name so field access on it resolves later. The RHS
            // initializer (when not the VOID_E placeholder) gets analyzed
            // so its embedded function calls receive `resolved_sign` —
            // codegen needs that to emit the call. Skipping the analysis
            // would land users with `EntityRef e = /* ERROR: unresolved
            // function CreateEntity */;` even though the analyzer accepted
            // the declaration.
            if (s->as.var_decl.varType == VAR_T) {
                StructDecl* sd = lookup_struct(g_struct_table,
                                               s->as.var_decl.typeName);
                if (!sd) {
                    stage_error(STAGE_ANALYZER, s->loc,
                                "unknown type '%s' for variable '%s'",
                                s->as.var_decl.typeName, s->as.var_decl.name);
                }
                if (s->as.var_decl.expr &&
                    s->as.var_decl.expr->type != VOID_E) {
                    (void)analyze_expr(scope, funcTable,
                                       s->as.var_decl.expr, currentFunc);
                }
                declare(scope,
                        s->as.var_decl.name,
                        VAR_T,
                        s->as.var_decl.ownership,
                        s->as.var_decl.isNullable,
                        s->as.var_decl.isConst,
                        s->as.var_decl.isArray,
                        s->as.var_decl.isArray
                            ? (s->as.var_decl.arraySize
                                && s->as.var_decl.arraySize->type == INT_LIT_E
                                ? s->as.var_decl.arraySize->as.int_val
                                : 0)
                            : 0);
                // Stamp type_name on the freshly-declared symbol so future
                // field accesses can resolve. lookup() returns the actual
                // slot — pointer is stable until the scope grows again.
                Symbol* sym = lookup(scope, s->as.var_decl.name);
                if (sym) sym->type_name = s->as.var_decl.typeName;
                break;
            }

            //skip type analysis for uninitialized arrays (VOID_E placeholder)
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

            //handle move semantics: if initializing own from another own variable, mark source as MOVED
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

            // nullable propagation: if rhs may be null (e.g. extern returning ptr?),
            // the declared variable must also be marked nullable so the user is
            // forced to handle the null case via `match` / null check.
            if (s->as.var_decl.expr->type != VOID_E &&
                s->as.var_decl.expr->is_nullable &&
                !s->as.var_decl.isNullable) {
                stage_error(STAGE_ANALYZER, s->loc,
                    "'%s' may be null but is not declared nullable - declare as `?%s` and handle with `match`",
                    s->as.var_decl.name, token_type_name(s->as.var_decl.varType));
            }

            if (s->as.var_decl.expr->type != VOID_E && t != s->as.var_decl.varType && !(s->as.var_decl.isNullable && t == NULL_LIT_T)) {
                //special case: allow assigning alloc[n] char to string
                bool isStringAlloc = (s->as.var_decl.varType == STR_KEYWORD_T && t == CHAR_KEYWORD_T && s->as.var_decl.expr->type == ALLOC_E);
                
                //numeric promotion: char -> int -> float -> double
                const bool isNumericPromotion =
                    numeric_compatible(s->as.var_decl.varType, t);

                if (!isStringAlloc && !isNumericPromotion) {
                    stage_error(STAGE_ANALYZER, s->loc, "variable '%s' declared as %s but initialized with %s",
                                s->as.var_decl.name, token_type_name(s->as.var_decl.varType), token_type_name(t));
                }
            }

            if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_NONE) {
                //check for VLA initialization (C limitation)
                if (s->as.var_decl.arraySize != NULL &&
                    s->as.var_decl.arraySize->type != INT_LIT_E &&
                    s->as.var_decl.expr->type == ARRAY_DECL_E) {
                    stage_error(STAGE_ANALYZER, s->loc,
                                "stack arrays with variable size cannot be initialized (C language limitation)");
                    stage_note(STAGE_ANALYZER, s->loc,
                               "use a constant size like 'arr: int[5] = {...}', or use heap allocation with 'own'");
                }

                //only require array literal for constant-sized arrays
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

            //set element ownership on the symbol
            if (s->as.var_decl.elementOwnership != OWNERSHIP_NONE) {
                Symbol* sym = lookup(scope, s->as.var_decl.name);
                if (sym) sym->element_ownership = s->as.var_decl.elementOwnership;
            }

            //for ref variables, set the owner to the variable being borrowed
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

        case FIELD_ASSIGN_S: {
            // target.field = value
            // Analyze the target → must yield a struct (VAR_T) with a known
            // type_name. Then validate the field exists and the value type
            // matches (with the usual numeric-promotion allowance).
            TokenType targetType = analyze_expr(scope, funcTable,
                                                 s->as.field_assign.target,
                                                 currentFunc);
            if (targetType != VAR_T) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "field assignment requires a struct target, got '%s'",
                            token_type_name(targetType));
                break;
            }
            const char* tname = s->as.field_assign.target->analyzed_type_name;
            StructDecl* sd = lookup_struct(g_struct_table, tname);
            if (!sd) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "unknown struct type '%s' on field assign",
                            tname ? tname : "(unnamed)");
                break;
            }
            StructField* fld = lookup_field(sd, s->as.field_assign.field_name);
            if (!fld) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "struct '%s' has no field '%s'",
                            sd->name, s->as.field_assign.field_name);
                break;
            }
            TokenType vt = analyze_expr(scope, funcTable,
                                         s->as.field_assign.value, currentFunc);
            if (!numeric_compatible(fld->type, vt)) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "field '%s.%s' is %s but assigned %s",
                            sd->name, fld->name,
                            token_type_name(fld->type),
                            token_type_name(vt));
            }
            // Same pointer-vs-value detection as FIELD_ACCESS_E. The
            // target expression is a pointer in C terms when:
            //   - it's a VAR_E referring to a nullable VAR_T symbol
            //     (e.g. `p: ?PlayerData`); OR
            //   - it's a FUNC_CALL_E whose extern signature has a
            //     nullable VAR_T return (e.g. `TimeManager()` returning
            //     `TimeManager?` -> emitted as `TimeManager*`).
            Expr* tgt = s->as.field_assign.target;
            if (tgt->type == VAR_E) {
                Symbol* tsym = lookup(scope, tgt->as.var.name);
                if (tsym && tsym->type == VAR_T &&
                    (tsym->is_nullable ||
                     tsym->ownership == OWNERSHIP_OWN ||
                     tsym->ownership == OWNERSHIP_REF))
                    s->as.field_assign.target_is_ptr = true;
            } else if (tgt->type == FUNC_CALL_E) {
                FuncSign* rs = tgt->as.func_call.resolved_sign;
                // Same rule as FIELD_ACCESS_E: pointer-typed return either
                // because of nullability OR ownership (`ref T` / `own T`).
                if (rs && rs->retType == VAR_T &&
                    (rs->retNullable ||
                     rs->retOwnership == OWNERSHIP_OWN ||
                     rs->retOwnership == OWNERSHIP_REF))
                    s->as.field_assign.target_is_ptr = true;
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
            if (t != sym->type) {
                //special case: allow assigning alloc[n] char to string
                bool isStringAlloc = (sym->type == STR_KEYWORD_T && t == CHAR_KEYWORD_T && s->as.var_assign.expr->type == ALLOC_E);
                
                const bool isNumericPromotion =
                    numeric_compatible(sym->type, t);

                if (!isStringAlloc && !isNumericPromotion) {
                    stage_error(STAGE_ANALYZER, s->loc, "cannot assign %s to '%s' of type %s",
                                token_type_name(t), s->as.var_assign.name, token_type_name(sym->type));
                }
            }

            //allow reassigning to freed own variables with alloc
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
            // Truthy-coerce: bool, int (any int kind), ptr, and nullable values
            // are all valid as conditions. C handles the runtime test for free
            // (non-zero / non-null = true), so we just relax the type check here.
            const bool truthy_ok =
                c == BOOL_KEYWORD_T || c == INT_KEYWORD_T ||
                c == CHAR_KEYWORD_T || c == PTR_KEYWORD_T ||
                s->as.if_stmt.cond->is_nullable;
            if (!truthy_ok)
                stage_error(STAGE_ANALYZER, s->loc,
                    "if condition must be bool, int, ptr, or nullable, got %s",
                    token_type_name(c));

            bool isSomeCheck = false;
            char* unwrappedVarName = nullptr;
            if (s->as.if_stmt.cond->type == SOME_E &&
                s->as.if_stmt.cond->as.some.var->type == VAR_E) {
                isSomeCheck = true;
                unwrappedVarName = s->as.if_stmt.cond->as.some.var->as.var.name;
            }
            // Shorthand: `if (pos)` where pos is a nullable variable acts like
            // `if (some(pos))` — inside the true branch, `pos` is unwrapped so
            // the user can dereference it without a `match`.
            if (!isSomeCheck &&
                s->as.if_stmt.cond->type == VAR_E &&
                s->as.if_stmt.cond->is_nullable) {
                isSomeCheck = true;
                unwrappedVarName = s->as.if_stmt.cond->as.var.name;
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
            const bool truthy_ok =
                c == BOOL_KEYWORD_T || c == INT_KEYWORD_T ||
                c == CHAR_KEYWORD_T || c == PTR_KEYWORD_T ||
                s->as.while_stmt.cond->is_nullable;
            if (!truthy_ok)
                stage_error(STAGE_ANALYZER, s->loc,
                    "while condition must be bool, int, ptr, or nullable, got %s",
                    token_type_name(c));
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
            const bool truthy_ok =
                c == BOOL_KEYWORD_T || c == INT_KEYWORD_T ||
                c == CHAR_KEYWORD_T || c == PTR_KEYWORD_T ||
                s->as.do_while_stmt.cond->is_nullable;
            if (!truthy_ok)
                stage_error(STAGE_ANALYZER, s->loc,
                    "do-while condition must be bool, int, ptr, or nullable, got %s",
                    token_type_name(c));
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
                    // The discriminant of a `match` IS the unwrap site,
                    // so it's allowed to refer to a nullable without
                    // having been unwrapped first. Temporarily mark as
                    // unwrapped to suppress the VAR_E "must be unwrapped"
                    // error inside analyze_expr; restore right after.
                    bool was_unwrapped = matchedSym->is_unwrapped;
                    matchedSym->is_unwrapped = true;
                    (void)analyze_expr(scope, funcTable, s->as.match_stmt.var, currentFunc);
                    matchedSym->is_unwrapped = was_unwrapped;
                } else {
                    stage_error(STAGE_ANALYZER, s->loc, "variable '%s' is not declared",
                                s->as.match_stmt.var->as.var.name);
                }
            } else {
                //for non-variable expressions, analyze normally
                matchedType = analyze_expr(scope, funcTable, s->as.match_stmt.var, currentFunc);
            }

            //determine if this is a nullable match. Two sources:
            //  - bare-variable match: matchedSym->is_nullable (from declaration)
            //  - expression match (e.g. match Get<X>(e) {...}): the call's
            //    is_nullable was set by FUNC_CALL_E analysis when the
            //    extern decl returns `T?`.
            bool isNullableMatch = (matchedSym && matchedSym->is_nullable) ||
                                   s->as.match_stmt.var->is_nullable;

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
                    hasSome = hasNull = true;  //wildcard covers both
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

                //for SOME_PATTERN, declare binding variable as non-nullable reference.
                //Two source shapes:
                //  - bare variable match (`match v { some(p): ... }`):
                //    matchedSym carries the type info.
                //  - expression match (`match e.Get<T>() { some(p): ... }`):
                //    matchedSym is null; pull the type from the analyzed
                //    expression. is_nullable + analyzed_type_name were
                //    set by FUNC_CALL_E analysis when the extern's
                //    retNullable was true.
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

                    //set owner for ref tracking (if its a ref)
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
                else if (branch->pattern->type == SOME_PATTERN &&
                         !matchedSym && isNullableMatch &&
                         s->as.match_stmt.var->analyzed_type_name) {
                    // Expression-match path: `match e.Get<T>() { some(t): ... }`.
                    // No source variable to consult; pull the type out
                    // of the analyzed expression. Bind `t` as a
                    // non-nullable VAR_T pointer-borrow in the branch
                    // scope. matchedType is VOID_KEYWORD_T here (we
                    // didn't have a Symbol), but we know the expr's
                    // analyzed_type_name names the underlying struct.
                    declare(branchScope,
                            branch->pattern->as.binding_name,
                            VAR_T,
                            OWNERSHIP_NONE,
                            false, false, false, 0);
                    Symbol* bindingSym =
                        lookup(branchScope, branch->pattern->as.binding_name);
                    if (bindingSym) {
                        bindingSym->type_name =
                            s->as.match_stmt.var->analyzed_type_name;
                        // The binding IS a pointer at the C ABI level
                        // (extern returns T*). Mark is_nullable so the
                        // FIELD_ACCESS_E pointer-target detection fires
                        // when the user writes `t.field`. is_unwrapped
                        // ensures the analyzer doesn't demand another
                        // unwrap on top of `some(t)`.
                        bindingSym->is_nullable = true;
                        bindingSym->is_unwrapped = true;
                    }
                    branch->analyzed_type = VAR_T;
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

            if (!sym->is_array && sym->type != STR_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "'%s' is not an array or string", s->as.array_elem_assign.arrayName);
                break;
            }

            if (sym->is_const) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "cannot modify const array/string '%s'", s->as.array_elem_assign.arrayName);
                break;
            }

            TokenType indexType = analyze_expr(scope, funcTable, s->as.array_elem_assign.index, currentFunc);
            if (indexType != INT_KEYWORD_T) {
                stage_error(STAGE_ANALYZER, s->loc,
                            "index must be 'int', got '%s'", token_type_name(indexType));
            }

            TokenType valueType = analyze_expr(scope, funcTable, s->as.array_elem_assign.value, currentFunc);
            
            TokenType elemType = sym->type;
            if (sym->type == STR_KEYWORD_T) elemType = CHAR_KEYWORD_T;

            if (valueType != elemType) {
                //allow implicit int -> char narrowing
                bool isNarrowing = (elemType == CHAR_KEYWORD_T && valueType == INT_KEYWORD_T);
                
                if (!isNarrowing) {
                    stage_error(STAGE_ANALYZER, s->loc,
                                "cannot assign '%s' to element of type '%s'",
                                token_type_name(valueType), token_type_name(elemType));
                }
            }

            break;
        }

        case FREE_S: {
            // Two forms: bare identifier (`free p;`) and one-level
            // field access (`free l.items;`). Split off the optional
            // ".field" tail for the symbol lookup -- ownership /
            // double-free tracking applies at the parent symbol
            // (the field is part of its struct).
            const char* name = s->as.free_stmt.varName;
            const char* dot  = strchr(name, '.');
            char base_name[128];
            if (dot) {
                int n = (int)(dot - name);
                if (n > (int)sizeof(base_name) - 1) n = sizeof(base_name) - 1;
                memcpy(base_name, name, n);
                base_name[n] = 0;
            } else {
                snprintf(base_name, sizeof(base_name), "%s", name);
            }
            Symbol* sym = lookup(scope, base_name);
            if (sym == nullptr) {
                stage_error(STAGE_ANALYZER, s->loc,
                    "cannot free '%s', variable not declared", base_name);
                break;
            }

            // Field-access path: trust the API contract for now. The
            // struct field can't yet carry `own` (parser limitation),
            // so the analyzer can't enforce ownership through the
            // dot. Skip the OWN/FREED/MOVED checks and just emit the
            // free at codegen time. Once `own` on struct fields lands,
            // tighten this branch.
            if (dot) {
                s->as.free_stmt.isArrayOfOwned = false;
                s->as.free_stmt.arraySize = 0;
                // base is a pointer in C iff it's an own/ref local --
                // codegen needs to emit `base->field` in that case
                // instead of `base.field`.
                s->as.free_stmt.target_is_ptr =
                    (sym->ownership == OWNERSHIP_OWN ||
                     sym->ownership == OWNERSHIP_REF);
                break;
            }

            //cHECK: Can only free own variables
            if (sym->ownership != OWNERSHIP_OWN)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', it is not an 'own' variable", s->as.free_stmt.varName);

            //cHECK: Double free
            if (sym->state == FREED)
                stage_error(STAGE_ANALYZER, s->loc, "double free: variable '%s' has already been freed", s->as.free_stmt.varName);

            //cHECK: Free after move
            if (sym->state == MOVED)
                stage_error(STAGE_ANALYZER, s->loc, "cannot free '%s', ownership has been moved", s->as.free_stmt.varName);

            //set cascading free info for codegen
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
    copy.name = strdup(func->signature->name);  //make a real copy of the name string
    copy.retType = func->signature->retType;
    copy.retTypeName = func->signature->retTypeName ? strdup(func->signature->retTypeName) : NULL;
    copy.retOwnership = func->signature->retOwnership;
    copy.paramNum = func->signature->paramNum;
    copy.isExtern = false;   // user-defined functions are never extern (was uninit)
    copy.retNullable = func->signature->retNullable;   // CRITICAL: leaving uninit causes
                                                       // every call to be treated as
                                                       // potentially-null garbage.
    // Mirror the OOP visibility / static metadata so the analyzer
    // can enforce private + static rules off the FuncSign array.
    copy.is_private   = func->signature->is_private;
    copy.is_static    = func->signature->is_static;
    copy.owner_struct = func->signature->owner_struct
                          ? strdup(func->signature->owner_struct) : NULL;

    //deep copy parameters array
    if (copy.paramNum > 0) {
        copy.parameters = malloc(sizeof(FuncParam) * copy.paramNum);
        for (int i = 0; i < copy.paramNum; i++) {
            copy.parameters[i] = func->signature->parameters[i];
            //param names also come from tokens, stay alive
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
                //this is a leak!
                stage_error(STAGE_ANALYZER, NO_LOC, "Memory leak: '%s' is not freed or moved", s->name);
            }
            //if state is MOVED or FREED, we are happy.
        }
    }
}

void analyze_program(Program* prog) {
    Scope* global = make_scope(nullptr);
    FuncTable* funcTable = make_funcTable();

    // Make `prog` available to the function-call analyzer for UFCS
    // method dispatch (which may need to queue+drain new template
    // instantiations on the fly).
    g_analyzer_program = prog;

    //initialize and process imports
    g_import_registry = make_import_registry();
    for (int i = 0; i < prog->imports->import_count; i++) {
        register_import(g_import_registry, prog->imports->imports[i]);
    }

    //register all top-level struct decls before anything else, so
    //field types referencing other structs (forward refs across the
    //file) resolve uniformly.
    g_struct_table = make_struct_table();
    for (int i = 0; i < prog->struct_count; i++) {
        // Skip raw templates — only their concrete monomorphisations carry
        // analysable field types. Templates with unresolved `T` fields would
        // fail the field-type validation below.
        if (prog->structs[i] && prog->structs[i]->type_params) continue;
        register_struct(g_struct_table, prog->structs[i]);
    }
    //second pass: validate that struct field types referring to other
    //structs (VAR_T fields) actually resolve. Cheap and catches typos
    //before any function body uses the struct.
    for (int i = 0; i < g_struct_table->count; i++) {
        StructDecl* d = g_struct_table->decls[i];
        for (int j = 0; j < d->field_count; j++) {
            StructField* f = &d->fields[j];
            if (f->type == VAR_T && !lookup_struct(g_struct_table, f->type_name)) {
                stage_error(STAGE_ANALYZER, d->loc,
                            "struct '%s' field '%s' has unknown type '%s'",
                            d->name, f->name, f->type_name);
            }
        }
    }

    //0. Register extern functions. Duplicate decls (same name) are
    //silently deduped -- multiple files often declare the same libc
    //symbol (e.g. `extern <math.h> { def sqrtf(...); }` in both the
    //engine prelude AND std.math). They both refer to the same C
    //function so accepting the first and skipping the rest is safe;
    //a hard error would force users to coordinate across modules.
    for (int i = 0; i < prog->ext_block_count; ++i) {
        ExternBlock* block = prog->externBlocks[i];
        for (int j = 0; j < block->count; ++j) {
            FuncSign* sign = block->signs[j];
            if(lookup_func_sign(funcTable, sign)) {
                continue;   // dedupe; first decl wins
            }
            if(funcTable->count >= funcTable->capacity) {
                funcTable->capacity *= 2;
                funcTable->signs = realloc(funcTable->signs, sizeof(FuncSign) * funcTable->capacity);
            }
            funcTable->signs[funcTable->count++] = *sign; //shallow copy struct
        }
    }

    Func** fs = prog->functions;
    int count = prog->func_count;

    //pre-allocate enough space for all functions to avoid realloc invalidating pointers
    //pre-allocate enough space for all functions to avoid realloc invalidating pointers
    int total_needed = funcTable->count + count;
    if (total_needed > funcTable->capacity) {
        funcTable->capacity = total_needed + 4; //add some buffer
        funcTable->signs = realloc(funcTable->signs, sizeof(FuncSign) * funcTable->capacity);
    }

    for (int i = 0; i < count; ++i) {
        // Skip raw function templates; only the concrete monomorphisations
        // (added to prog->functions by tpl_drain_pending) get analysed.
        if (fs[i] && fs[i]->type_params) continue;
        defineAndAnalyzeFunc(funcTable, fs[i]);
    }
    for (int i = 0; i < count; ++i) {
        if (fs[i] && fs[i]->type_params) continue;   // skip raw templates
        Scope* funcScope = make_scope(global);

        for (int j = 0; j < fs[i]->signature->paramNum; ++j) {
            FuncParam* fp = &fs[i]->signature->parameters[j];
            declare(funcScope, fp->name, fp->type, fp->ownership,
                    fp->isNullable, fp->isConst, false, 0);
            // Struct + fn-pointer params: stamp the symbol's auxiliary
            // type info so subsequent expressions resolve correctly.
            Symbol* sym = lookup(funcScope, fp->name);
            if (sym) {
                if (fp->type == VAR_T) sym->type_name = fp->type_name;
                if (fp->type == FN_T)  sym->fn_sig    = fp->fn_sig;
            }
        }

        analyze_stmt(funcScope, funcTable, fs[i]->body, fs[i]->signature);

        check_function_cleanup(funcScope);

        free(funcScope->symbols);
        free(funcScope);
    }

    //dONT free funcTable yet - codegen needs the resolved_sign pointers!
    //tODO: Free after codegen or store FuncTable in Program
    //free(funcTable->signs);
    //free(funcTable);

    free(global->symbols);
    free(global);
}