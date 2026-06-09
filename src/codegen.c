//created by bucka on 2/9/2026.

#include "codegen.h"
#include <string.h>

char* type_to_c_type(TokenType t) {
    switch (t) {
        case INT_KEYWORD_T: return "int";
        case USIZE_KEYWORD_T: return "size_t";  // matches libc width on every
                                                // platform; needs <stddef.h>
                                                // (always emitted at file top
                                                // by the auto-include block).
        case BOOL_KEYWORD_T: return "bool";
        case CHAR_KEYWORD_T: return "char";
        case STR_KEYWORD_T: return "char*";
        case FLOAT_KEYWORD_T: return "float";
        case DOUBLE_KEYWORD_T: return "double";
        case VOID_KEYWORD_T: return "void";
        case PTR_KEYWORD_T:  return "void*";   // opaque pointer
        default: return "-UNKNOWN-";
    }
}

// Emit a C function-pointer declarator: "RetType (*name)(P1, P2, ...)".
// `name` may be empty (parameter without a name in fn-type position).
// Used by var-decl + func-param emission for FN_T types.
static void emit_fn_ptr_decl(FuncSign* sig, const char* name, FILE* out);

// Render an attribute list as C comments. Plugins later read these by
// scanning the .c output (or, properly, via the plugin AST API once that
// lands). Emitting is no-op when list is NULL or empty.
static void emit_attr_list(const AttributeList* list, FILE* out) {
    if (!list || list->count == 0) return;
    // Known attributes get translated into C decorators that flow
    // before the function's return type. Unknown attributes still
    // round-trip as `// @lync_attr Name(args)` comments so they're
    // visible in the generated source for engine-side tooling.
    for (int i = 0; i < list->count; ++i) {
        const Attribute* a = list->items[i];
        if (a->name && strcmp(a->name, "inline")     == 0) {
            fprintf(out, "static inline ");
            continue;
        }
        if (a->name && strcmp(a->name, "noinline")   == 0) {
            fprintf(out, "__attribute__((noinline)) ");
            continue;
        }
        if (a->name && strcmp(a->name, "pure")       == 0) {
            fprintf(out, "__attribute__((pure)) ");
            continue;
        }
        if (a->name && strcmp(a->name, "const_attr") == 0) {
            // Even purer than `pure` -- no global memory reads either.
            // Names it `const_attr` because `const` is a Lync keyword.
            fprintf(out, "__attribute__((const)) ");
            continue;
        }
        fprintf(out, "// @lync_attr %s", a->name);
        if (a->arg_count > 0) {
            fprintf(out, "(");
            for (int j = 0; j < a->arg_count; ++j) {
                if (j > 0) fprintf(out, ", ");
                const AttrArg* g = &a->args[j];
                switch (g->kind) {
                    case ATTR_ARG_INT:    fprintf(out, "%d", g->int_val); break;
                    case ATTR_ARG_BOOL:   fprintf(out, "%s", g->int_val ? "true" : "false"); break;
                    case ATTR_ARG_STRING: fprintf(out, "\"%s\"", g->str_val ? g->str_val : ""); break;
                }
            }
            fprintf(out, ")");
        }
        fprintf(out, "\n");
    }
}

static const char* type_for_fn_param(FuncParam* p) {
    if (p->type == VAR_T && p->type_name) return p->type_name;
    return type_to_c_type(p->type);
}

static void emit_fn_ptr_decl(FuncSign* sig, const char* name, FILE* out) {
    // Return type. For struct returns we'd use sig->retTypeName; v1 doesn't
    // exercise that path through fn-pointers.
    fprintf(out, "%s (*%s)(",
            sig->retType == VAR_T && sig->retTypeName
                ? sig->retTypeName
                : type_to_c_type(sig->retType),
            name ? name : "");
    if (sig->paramNum == 0) {
        fprintf(out, "void");
    } else {
        for (int i = 0; i < sig->paramNum; ++i) {
            if (i > 0) fprintf(out, ", ");
            FuncParam* p = &sig->parameters[i];
            if (p->type == FN_T && p->fn_sig) {
                // Nested fn-type param. Emit a fully unnamed callable.
                emit_fn_ptr_decl(p->fn_sig, "", out);
            } else {
                fprintf(out, "%s", type_for_fn_param(p));
            }
        }
    }
    fprintf(out, ")");
}

// Struct-aware variants. Func params + struct fields can carry a struct
// type, where the C type IS the struct name (we typedef'd it that way).
// Pure C primitives fall through to type_to_c_type / token_type_name.
static const char* c_type_for(TokenType t, const char* type_name) {
    if (t == VAR_T && type_name) return type_name;
    return type_to_c_type(t);
}

static const char* mangle_type_for(TokenType t, const char* type_name) {
    if (t == VAR_T && type_name) return type_name;
    return token_type_name(t);
}

//emit indentation (2 spaces per level)
void emit_indent(FILE* out, int level) {
    for (int i = 0; i < level; i++) {
        fprintf(out, "  ");
    }
}

void emit_type(TokenType type, FILE* out) {
    switch (type) {
        case INT_KEYWORD_T: fprintf(out, "int"); break;
        case USIZE_KEYWORD_T: fprintf(out, "size_t"); break;
        case BOOL_KEYWORD_T: fprintf(out, "bool"); break;
        case STR_KEYWORD_T: fprintf(out, "char"); break;
        case CHAR_KEYWORD_T: fprintf(out, "char"); break;
        case FLOAT_KEYWORD_T: fprintf(out, "float"); break;
        case DOUBLE_KEYWORD_T: fprintf(out, "double"); break;
        case VOID_KEYWORD_T: fprintf(out, "void"); break;
        default: fprintf(out, "void"); break;
    }
}

bool emit_pattern_condition(Pattern* pattern, Expr* matchVar, FILE* out, FuncSignToName* fstn) {
    switch (pattern->type) {
        case NULL_PATTERN:
            //for nullable pointers, check the pointer itself, not dereferenced value
            if (matchVar->type == VAR_E) {
                fprintf(out, "%s", matchVar->as.var.name);
            } else {
                emit_expr(matchVar, out, fstn);
            }
            fprintf(out, " == NULL");
            return false;
        case SOME_PATTERN:
            //for nullable pointers, check the pointer itself, not dereferenced value
            if (matchVar->type == VAR_E) {
                fprintf(out, "%s", matchVar->as.var.name);
            } else {
                emit_expr(matchVar, out, fstn);
            }
            fprintf(out, " != NULL");
            return false;
        case VALUE_PATTERN:
            emit_expr(matchVar, out, fstn);
            fprintf(out, " == ");
            emit_expr(pattern->as.value_expr, out, fstn);
            return false;
        case WILDCARD_PATTERN:
            return true;
    }
    return false;
}

char* get_mangled_name(FuncSign* sign) {
    static char buffer[512];
    char* ptr = buffer;

    stage_trace(STAGE_CODEGEN, "get_mangled_name entry: sign=%p", sign);

    if (sign == NULL) {
        strcpy(buffer, "NULL_SIGN");
        return buffer;
    }

    if (sign->isExtern) {
        return sign->name;
    }

    //read the name pointer without dereferencing the string yet
    char* name_ptr = sign->name;
    stage_trace(STAGE_CODEGEN, "name pointer value: %p", name_ptr);
    if (name_ptr == NULL) {
        strcpy(buffer, "NULL_NAME");
        return buffer;
    }
    stage_trace(STAGE_CODEGEN, "about to dereference name");
    stage_trace(STAGE_CODEGEN, "name is: %s", name_ptr);

    stage_trace(STAGE_CODEGEN, "name is: %s", sign->name);
    ptr += sprintf(ptr, "%s", sign->name);

    stage_trace(STAGE_CODEGEN, "adding return type");
    ptr += sprintf(ptr, "_%s", token_type_name(sign->retType));

    stage_trace(STAGE_CODEGEN, "adding %d parameters", sign->paramNum);
    for (int i = 0; i < sign->paramNum; i++) {
        stage_trace(STAGE_CODEGEN, "adding param %d", i);
        ptr += sprintf(ptr, "_%s", mangle_type_for(sign->parameters[i].type,
                                                    sign->parameters[i].type_name));
        if (sign->parameters[i].ownership != OWNERSHIP_NONE) {
            ptr += sprintf(ptr, "%s",
                           sign->parameters[i].ownership == OWNERSHIP_OWN ? "own" : "ref");
        }
    }

    stage_trace(STAGE_CODEGEN, "get_mangled_name done: %s", buffer);
    return buffer;
}

//hash-based version to keep names shorter
uint32_t hash_signature(FuncSign* sign) {
    uint32_t hash = 5381;

    //hash name
    for (char* s = sign->name; *s; s++) {
        hash = ((hash << 5) + hash) + *s;
    }

    //hash return type
    hash = ((hash << 5) + hash) + sign->retType;

    //hash parameters
    for (int i = 0; i < sign->paramNum; i++) {
        hash = ((hash << 5) + hash) + sign->parameters[i].type;
        hash = ((hash << 5) + hash) + sign->parameters[i].ownership;
    }

    return hash;
}

char* get_mangled_name_short(FuncSign* sign) {
    static char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s_%x", sign->name, hash_signature(sign));
    return buffer;
}

char* get_func_name_from_sign(FuncSignToName* fstn, FuncSign* sign) {
    for (int i = 0; i < fstn->count; ++i) {
        if(check_func_sign(fstn->elements[i].sign, sign))
            return fstn->elements[i].name;
    }
    return "--NO_GOOD_SIGN--";
}

char* get_type_signature(FuncSign* sign) {
    static char buffer[256];
    char* ptr = buffer;

    ptr += sprintf(ptr, "%s_", token_type_name(sign->retType));

    for (int i = 0; i < sign->paramNum; i++) {
        if (i > 0) ptr += sprintf(ptr, "_");
        ptr += sprintf(ptr, "%s", mangle_type_for(sign->parameters[i].type,
                                                   sign->parameters[i].type_name));
        if (sign->parameters[i].ownership != OWNERSHIP_NONE) {
            ptr += sprintf(ptr, "%s",
                           sign->parameters[i].ownership == OWNERSHIP_OWN ? "own" : "ref");
        }
    }

    return buffer;
}

// ---- defer support --------------------------------------------------------
//
// Function-scope `defer` collects all deferred statements during a
// pre-walk of the function body, replaces every `return` with
// `goto __zues_cleanup`, and emits the deferred bodies in LIFO order
// at the cleanup label before the actual return. The visitors stash
// state in a tiny module-global so emit_stmt can detect "skip me, I'm
// a defer" and intercept FUNC_RET_E without threading state through
// the entire codegen API.
static Stmt** g_defers           = NULL;
static int    g_defer_count      = 0;
static int    g_defer_capacity   = 0;
static bool   g_func_has_defers  = false;
static TokenType g_func_ret_type = VOID_KEYWORD_T;
static char*  g_func_ret_typename = NULL;

static void collect_defers(Stmt* s) {
    if (!s) return;
    switch (s->type) {
        case DEFER_S:
            if (g_defer_count >= g_defer_capacity) {
                g_defer_capacity = g_defer_capacity ? g_defer_capacity * 2 : 4;
                g_defers = realloc(g_defers, sizeof(Stmt*) * g_defer_capacity);
            }
            g_defers[g_defer_count++] = s->as.defer_stmt.body;
            break;
        case BLOCK_S:
            for (int i = 0; i < s->as.block_stmt.count; ++i)
                collect_defers(s->as.block_stmt.stmts[i]);
            break;
        case IF_S:
            collect_defers(s->as.if_stmt.trueStmt);
            collect_defers(s->as.if_stmt.falseStmt);
            break;
        case WHILE_S:    collect_defers(s->as.while_stmt.body);    break;
        case DO_WHILE_S: collect_defers(s->as.do_while_stmt.body); break;
        case FOR_S:      collect_defers(s->as.for_stmt.body);      break;
        case MATCH_S:
            for (int i = 0; i < s->as.match_stmt.branchCount; ++i) {
                MatchBranchStmt* b = &s->as.match_stmt.branches[i];
                for (int j = 0; j < b->stmtCount; ++j)
                    collect_defers(b->stmts[j]);
            }
            break;
        default: break;
    }
}

void emit_func(Func* f, FILE* out, FuncSignToName* fstn) {
    stage_trace(STAGE_CODEGEN, "emit_func: %s", f->signature->name);

    // Reset defer collection state for this function.
    g_defer_count       = 0;
    g_func_has_defers   = false;
    g_func_ret_type     = f->signature->retType;
    g_func_ret_typename = f->signature->retTypeName;
    if (f->body) collect_defers(f->body);
    g_func_has_defers = (g_defer_count > 0);

    emit_attr_list(f->attrs, out);
    if(strcmp(f->signature->name, "main") == 0) fprintf(out, "int");
    else if (f->signature->retType == VAR_T && f->signature->retTypeName) {
        // Struct return: emit the typedef name. type_to_c_type has no
        // VAR_T case (returns "-UNKNOWN-") -- without this, every
        // function whose return type is a templated struct (List<int>,
        // ...) generates broken C.
        fprintf(out, "%s%s", f->signature->retTypeName,
            f->signature->retOwnership != OWNERSHIP_NONE ? "*" : "");
    }
    else fprintf(out, "%s%s", type_to_c_type(f->signature->retType), (f->signature->retOwnership != OWNERSHIP_NONE && f->signature->retType != STR_KEYWORD_T) ? "*" : "");
    fprintf(out, " %s(", strcmp(f->signature->name, "main") == 0 ? "main" : get_func_name_from_sign(fstn, f->signature));

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        FuncParam* p = &f->signature->parameters[i];
        if (p->type == FN_T && p->fn_sig) {
            // fn-typed param — emit as a C function-pointer declarator
            // with the param name baked into the (*name) slot.
            emit_fn_ptr_decl(p->fn_sig, p->name, out);
        } else {
            // Same pointer-suffix rule as emit_func_decl above: ownership
            // marker OR nullable-struct param both demand a `*`.
            const bool ptr_suffix =
                (p->ownership != OWNERSHIP_NONE && p->type != STR_KEYWORD_T)
                || (p->isNullable && p->type == VAR_T);
            fprintf(out, "%s%s",
                c_type_for(p->type, p->type_name),
                ptr_suffix ? "*" : "");
            fprintf(out, " %s", p->name);
        }
    }
    fprintf(out, ")\n");

    stage_trace(STAGE_CODEGEN, "emit_func: calling emit_stmt for body");
    if (g_func_has_defers && f->body && f->body->type == BLOCK_S) {
        // Custom body emit so we can inject `__zues_ret` declaration
        // at the top and the cleanup label at the bottom.
        fprintf(out, "{\n");
        // Reserve __zues_ret only when the function returns a value.
        const bool void_ret = (f->signature->retType == VOID_KEYWORD_T);
        if (!void_ret) {
            emit_indent(out, 1);
            const bool ret_is_struct = (f->signature->retType == VAR_T &&
                                          f->signature->retTypeName);
            if (ret_is_struct) {
                fprintf(out, "%s%s __zues_ret = {0};\n",
                        f->signature->retTypeName,
                        f->signature->retOwnership != OWNERSHIP_NONE ? "*" : "");
            } else {
                fprintf(out, "%s%s __zues_ret = 0;\n",
                        type_to_c_type(f->signature->retType),
                        (f->signature->retOwnership != OWNERSHIP_NONE &&
                         f->signature->retType != STR_KEYWORD_T) ? "*" : "");
            }
        }
        for (int i = 0; i < f->body->as.block_stmt.count; ++i) {
            emit_stmt(f->body->as.block_stmt.stmts[i], out, 1, fstn);
        }
        // Cleanup tail. Defers run in LIFO order; an unreachable label
        // warning is sidestepped because every return path goes through
        // it. The trailing return matches the function's signature.
        fprintf(out, "__zues_cleanup:;\n");
        for (int i = g_defer_count - 1; i >= 0; --i) {
            emit_stmt(g_defers[i], out, 1, fstn);
        }
        if (void_ret) {
            emit_indent(out, 1);
            fprintf(out, "return;\n");
        } else {
            emit_indent(out, 1);
            fprintf(out, "return __zues_ret;\n");
        }
        fprintf(out, "}\n");
    } else {
        emit_stmt(f->body, out, 0, fstn);
    }
    stage_trace(STAGE_CODEGEN, "emit_func: done with %s", f->signature->name);
}

void emit_func_decl(Func* f, FILE* out, FuncNameCounter* fnc, FuncSignToName* fstn) {
    if(strcmp(f->signature->name, "main") == 0) return;

    int funcNum = -1;
    char* origName = f->signature->name;

    for (int i = 0; i < fnc->count; ++i) {
        if (strcmp(fnc->elements[i].name, origName) == 0) {
            funcNum = ++fnc->elements[i].count;
            if(fnc->count >= fnc->height) {
                fnc->height *= 2;
                fnc->elements = realloc(fnc->elements, sizeof(FuncNameCounterElement) * fnc->height);
            }
        }
    }
    if(funcNum == -1) {
        fnc->elements[fnc->count++] = (FuncNameCounterElement){.count = 0, .name = origName};
        if(fnc->count >= fnc->height) {
            fnc->height *= 2;
            fnc->elements = realloc(fnc->elements, sizeof(FuncNameCounterElement) * fnc->height);
        }
        funcNum = 0;
    }

    char* mangled = get_mangled_name(f->signature);
    char* bufP = strdup(mangled);

    fstn->elements[fstn->count++] = (FuncSignToNameElement){.sign = f->signature, .name = bufP};

    if(fstn->count >= fstn->height) {
        fstn->height *= 2;
        fstn->elements = realloc(fstn->elements, sizeof(FuncSignToNameElement) * fstn->height);
    }

    // Mirror attributes onto the prototype so the C compiler doesn't
    // complain about a `static inline` or `__attribute__((pure))`
    // definition declared without those qualifiers.
    emit_attr_list(f->attrs, out);
    if (f->signature->retType == VAR_T && f->signature->retTypeName) {
        fprintf(out, "%s%s", f->signature->retTypeName,
            f->signature->retOwnership != OWNERSHIP_NONE ? "*" : "");
    } else {
        fprintf(out, "%s%s", type_to_c_type(f->signature->retType), (f->signature->retOwnership != OWNERSHIP_NONE && f->signature->retType != STR_KEYWORD_T) ? "*" : "");
    }
    fprintf(out, " %s(", mangled);

    for (int i = 0; i < f->signature->paramNum; ++i) {
        if(i > 0) fprintf(out, ", ");
        {
            FuncParam* __p = &f->signature->parameters[i];
            if (__p->type == FN_T && __p->fn_sig) {
                emit_fn_ptr_decl(__p->fn_sig, __p->name, out);
                continue;
            }
            // Pointer suffix for either an ownership marker (`own`/`ref`)
            // OR a nullable struct (`p: PlayerData?`). Nullable structs
            // round-trip through pointers on the C side -- the
            // analyser's match-unwrap path emits `if (p != NULL)` etc.,
            // which only typechecks if `p` is actually a pointer.
            const bool ptr_suffix =
                (__p->ownership != OWNERSHIP_NONE && __p->type != STR_KEYWORD_T)
                || (__p->isNullable && __p->type == VAR_T);
            fprintf(out, "%s%s",
                c_type_for(__p->type, __p->type_name),
                ptr_suffix ? "*" : "");
        }
        fprintf(out, " %s", f->signature->parameters[i].name);
    }
    fprintf(out, ");\n");
}

void emit_expr(Expr* e, FILE* out, FuncSignToName* fstn) {
    if (e == NULL) return;

    stage_trace(STAGE_CODEGEN, "emit_expr: type=%d", e->type);

    switch (e->type) {
        case INT_LIT_E:
            fprintf(out, "%d", e->as.int_val);
            break;

        case SIZEOF_E:
            // Primitive operand -> emit C type via type_to_c_type. Struct
            // operand (VAR_T) -> emit the type name directly; Lync
            // typedefs every struct as `typedef struct { ... } Name;`.
            if (e->as.sizeof_op.operand_type == VAR_T) {
                fprintf(out, "sizeof(%s)",
                    e->as.sizeof_op.operand_type_name
                        ? e->as.sizeof_op.operand_type_name : "void");
            } else {
                fprintf(out, "sizeof(%s)",
                    type_to_c_type(e->as.sizeof_op.operand_type));
            }
            break;

        case ADDR_OF_E:
            // Parenthesise so the address-of binds tightly to the operand
            // and not to whatever follows in the surrounding expression.
            fprintf(out, "&(");
            emit_expr(e->as.addr_of.target, out, fstn);
            fprintf(out, ")");
            break;

        case FIELD_ACCESS_E:
            // Emit target then `.field` (or `->field` when target is a
            // pointer in C terms — nullable VAR_T param with the
            // analyzer-set `target_is_ptr` flag). Recursive-friendly:
            // nested chain (a.b.c) recurses through the same logic for
            // each step.
            //
            // Suppress the auto-deref `*` that VAR_E normally prepends for
            // owned vars: when target is a VAR_E with ownership and we're
            // about to emit `->`, the dereference is already handled by
            // `->` itself. Without this, `p.field` on a `ref T` param
            // would emit `*p->field` and the C compiler chokes (`unary *
            // on float`).
            if (e->as.field_access.target_is_ptr
                && e->as.field_access.target->type == VAR_E) {
                fprintf(out, "%s", e->as.field_access.target->as.var.name);
            } else {
                emit_expr(e->as.field_access.target, out, fstn);
            }
            fprintf(out, "%s%s",
                e->as.field_access.target_is_ptr ? "->" : ".",
                e->as.field_access.field_name);
            break;

        case STRUCT_LIT_E: {
            // Emit a C99 designated-initializer compound literal. Lync
            // emits structs as `typedef struct { ... } Name;` so the cast
            // prefix is just the typedef name.
            // Missing fields are implicitly zero-initialised by C, matching
            // our analyzer's "missing field is OK" rule.
            fprintf(out, "(%s){", e->as.struct_lit.type_name);
            for (int i = 0; i < e->as.struct_lit.field_count; ++i) {
                if (i) fprintf(out, ", ");
                fprintf(out, ".%s = ", e->as.struct_lit.field_names[i]);
                emit_expr(e->as.struct_lit.field_values[i], out, fstn);
            }
            fprintf(out, "}");
            break;
        }

        case FLOAT_LIT_E: {
            // %g strips trailing zeros AND the decimal point for whole
            // numbers — `0.0` prints as `0`, which combined with the `f`
            // suffix below would yield `0f` (not a valid C float literal).
            // Re-add `.0` when the formatted value has no decimal/exponent.
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", e->as.double_val);
            fprintf(out, "%s", buf);
            const bool has_dot_or_exp = strchr(buf, '.') || strchr(buf, 'e')
                                     || strchr(buf, 'E') || strchr(buf, 'n')
                                     || strchr(buf, 'N');   // nan/inf
            if (!has_dot_or_exp) fprintf(out, ".0");
            if (e->analyzedType == FLOAT_KEYWORD_T) fprintf(out, "f");
            break;
        }

        case BOOL_LIT_E:
            fprintf(out, e->as.bool_val ? "true" : "false");
            break;

        case STR_LIT_E: {
            fprintf(out, "\"");
            for (char* s = e->as.str_val; *s != '\0'; s++) {
                switch (*s) {
                    case '\n': fprintf(out, "\\n"); break;
                    case '\t': fprintf(out, "\\t"); break;
                    case '\r': fprintf(out, "\\r"); break;
                    case '\\': fprintf(out, "\\\\"); break;
                    case '"': fprintf(out, "\\\""); break;
                    default: fprintf(out, "%c", *s); break;
                }
            }
            fprintf(out, "\"");
            break;
        }

        case CHAR_LIT_E: {
            fprintf(out, "'");
            char c = e->as.char_val;
            switch (c) {
                case '\n': fprintf(out, "\\n"); break;
                case '\t': fprintf(out, "\\t"); break;
                case '\r': fprintf(out, "\\r"); break;
                case '\0': fprintf(out, "\\0"); break;
                case '\\': fprintf(out, "\\\\"); break;
                case '\'': fprintf(out, "\\'"); break;
                default: fprintf(out, "%c", c); break;
            }
            fprintf(out, "'");
            break;
        }

        case NULL_LIT_E: {
            fprintf(out, "NULL");
            break;
        }

        case VAR_E:
            // Function-name-as-value: analyzer attached the resolved sig.
            // Emit the mangled C name via fstn so signatures match the
            // function definition emitted earlier.
            if (e->analyzed_fn_sig) {
                fprintf(out, "%s",
                        get_func_name_from_sign(fstn, e->analyzed_fn_sig));
                break;
            }
            fprintf(out, "%s%s", (e->as.var.ownership != OWNERSHIP_NONE && e->analyzedType != STR_KEYWORD_T) ? "*" : "", e->as.var.name);
            break;

        case UN_OP_E: {
            fprintf(out, "(");
            if (e->as.un_op.op == MINUS_T) {
                fprintf(out, "-");
            } else if (e->as.un_op.op == NEGATION_T) {
                fprintf(out, "!");
            }
            emit_expr(e->as.un_op.expr, out, fstn);
            fprintf(out, ")");
            break;
        }

        case BIN_OP_E: {
            //emit: (left op right)
            fprintf(out, "(");
            emit_expr(e->as.bin_op.exprL, out, fstn);

            switch (e->as.bin_op.op) {
                case PLUS_T: fprintf(out, " + "); break;
                case MINUS_T: fprintf(out, " - "); break;
                case STAR_T: fprintf(out, " * "); break;
                case SLASH_T: fprintf(out, " / "); break;
                case PERCENT_T: fprintf(out, " %% "); break;
                case DOUBLE_EQUALS_T: fprintf(out, " == "); break;
                case NOT_EQUALS_T: fprintf(out, " != "); break;
                case LESS_T: fprintf(out, " < "); break;
                case MORE_T: fprintf(out, " > "); break;
                case LESS_EQUALS_T: fprintf(out, " <= "); break;
                case MORE_EQUALS_T: fprintf(out, " >= "); break;
                case AND_T: fprintf(out, " && "); break;
                case OR_T: fprintf(out, " || "); break;
                case BIT_AND_T: fprintf(out, " & "); break;
                case BIT_OR_T: fprintf(out, " | "); break;
                case BIT_XOR_T: fprintf(out, " ^ "); break;
                case SHL_T: fprintf(out, " << "); break;
                case SHR_T: fprintf(out, " >> "); break;
                default: fprintf(out, " ??? "); break;
            }

            emit_expr(e->as.bin_op.exprR, out, fstn);
            fprintf(out, ")");
            break;
        }

        case FUNC_CALL_E: {
            //special handling for length() with strings -> strlen()
            if (strcmp(e->as.func_call.name, "length") == 0) {
                 fprintf(out, "strlen(");
                 emit_expr(e->as.func_call.params[0], out, fstn);
                 fprintf(out, ")");
                 return;
            }


            //handle std.io read_* functions
            if (strcmp(e->as.func_call.name, "read_int") == 0) {
                fprintf(out, "read_int()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_str") == 0) {
                fprintf(out, "read_str()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_bool") == 0) {
                fprintf(out, "read_bool()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_char") == 0) {
                fprintf(out, "read_char()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_key") == 0) {
                fprintf(out, "read_key()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_float") == 0) {
                fprintf(out, "read_float()");
                break;
            } else if (strcmp(e->as.func_call.name, "read_double") == 0) {
                fprintf(out, "read_double()");
                break;
            }

            if(strcmp(e->as.func_call.name, "print") == 0) {

                // Wrap in (printf(...), fflush(stdout)) so debug output
                // appears immediately. Without the flush, stdout buffering
                // inside a DLL can swallow print() before the host process
                // sees it (different CRT instance, different buffer).
                fprintf(out, "(printf(\"");

                for (int i = 0; i < e->as.func_call.count; ++i) {
                    Expr* p = e->as.func_call.params[i];

                    if (p->analyzedType == INT_KEYWORD_T) {
                        fprintf(out, "%%d");
                    } else if (p->analyzedType == BOOL_KEYWORD_T) {
                        fprintf(out, "%%s");
                    } else if (p->analyzedType == STR_KEYWORD_T) {
                        fprintf(out, "%%s");
                    } else if (p->analyzedType == CHAR_KEYWORD_T) {
                        fprintf(out, "%%c");
                    } else if (p->analyzedType == FLOAT_KEYWORD_T || p->analyzedType == DOUBLE_KEYWORD_T) {
                        fprintf(out, "%%g");
                    }

                    if (i < e->as.func_call.count - 1) {
                        fprintf(out, " ");
                    }
                }
                fprintf(out, "\\n\"");

                for (int i = 0; i < e->as.func_call.count; ++i) {
                    Expr* p = e->as.func_call.params[i];
                    fprintf(out, ", ");

                    if (p->analyzedType == BOOL_KEYWORD_T) {
                        fprintf(out, "(");
                        emit_expr(p, out, fstn);
                        fprintf(out, " ? \"true\" : \"false\")");
                    } else {
                        emit_expr(p, out, fstn);
                    }
                }

                fprintf(out, "), fflush(stdout))");
                return;
            }

            //regular function call
            if (e->as.func_call.resolved_sign == NULL) {
                fprintf(out, "/* ERROR: unresolved function %s */", e->as.func_call.name);
                break;
            }

            stage_trace(STAGE_CODEGEN, "emitting regular function call: %s", e->as.func_call.name);

            //try to safely access resolved_sign
            FuncSign* rs = e->as.func_call.resolved_sign;
            stage_trace(STAGE_CODEGEN, "resolved_sign pointer: %p", rs);

            // If the resolved sig is anonymous (no name), the call is
            // through a fn-pointer LOCAL of name e->as.func_call.name.
            // C lets us call function pointers exactly like functions, so
            // just emit the local's name verbatim.
            if (rs && rs->name == NULL) {
                fprintf(out, "%s(", e->as.func_call.name);
            } else {
                char* mangled_name = get_mangled_name(rs);
                stage_trace(STAGE_CODEGEN, "mangled name: %s", mangled_name);
                fprintf(out, "%s(", mangled_name);
            }
            for (int i = 0; i < e->as.func_call.count; ++i) {
                stage_trace(STAGE_CODEGEN, "emitting parameter %d", i);
                if (i != 0) fprintf(out, ", ");

                // Pass-through rules for `ref T` params:
                //   arg is VAR_E ownership=NONE  ->  emit "&(name)"
                //   arg is VAR_E ownership=REF/OWN -> emit "name" (no
                //     auto-deref; bypasses the default VAR_E emit which
                //     would write "*name")
                //   anything else  ->  fall through to emit_expr
                Expr* a = e->as.func_call.params[i];
                bool is_ref_param =
                    (rs && rs->parameters && i < rs->paramNum &&
                     rs->parameters[i].ownership == OWNERSHIP_REF);
                bool handled = false;
                if (is_ref_param && a->type == VAR_E) {
                    if (a->as.var.ownership == OWNERSHIP_NONE) {
                        fprintf(out, "&(%s)", a->as.var.name);
                        handled = true;
                    } else {
                        // already a pointer in C terms -- pass it raw,
                        // skipping the default VAR_E auto-deref.
                        fprintf(out, "%s", a->as.var.name);
                        handled = true;
                    }
                }
                if (!handled) emit_expr(a, out, fstn);
            }
            fprintf(out, ")");
            stage_trace(STAGE_CODEGEN, "done with function call: %s", e->as.func_call.name);
            break;
        }

        case FUNC_RET_E: {
            //if its a simple return, keep it on one line.
            //if its a match, use the temporary variable block.
            if (e->as.func_ret_expr->type == MATCH_E) {
                emit_indent(out, 0);
                fprintf(out, "{\n");
                emit_indent(out, 0 + 1);
                fprintf(out, "int _ret;\n");
                emit_assign_expr_to_var(e->as.func_ret_expr, "_ret", OWNERSHIP_NONE, out, 0 + 1, fstn);
                emit_indent(out, 0 + 1);
                fprintf(out, "return _ret;\n");
                emit_indent(out, 0);
                fprintf(out, "}\n");
            } else if (e->as.func_ret_expr->type == VOID_E) {
                emit_indent(out, 0);
                fprintf(out, "return");
            } else {
                emit_indent(out, 0);
                fprintf(out, "return ");
                if (e->as.func_ret_expr->type == VAR_E && e->as.func_ret_expr->as.var.ownership == OWNERSHIP_OWN) {
                    fprintf(out, "%s", e->as.func_ret_expr->as.var.name);
                } else {
                    emit_expr(e->as.func_ret_expr, out, fstn);
                }
            }
            break;
        }

        case SOME_E: {
            //for nullable pointers, dont dereference - check the pointer itself
            if (e->as.some.var->type == VAR_E) {
                fprintf(out, "%s != NULL", e->as.some.var->as.var.name);
            } else {
                fprintf(out, "(");
                emit_expr(e->as.some.var, out, fstn);
                fprintf(out, ") != NULL");
            }
            break;
        }

        case ALLOC_E: {
            break;
        }

        case ARRAY_DECL_E: {
            fprintf(out, "{");
            for (int i = 0; i < e->as.arr_decl.count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(e->as.arr_decl.values[i], out, fstn);
            }
            fprintf(out, "}");
            break;
        }

        case ARRAY_ACCESS_E: {
            fprintf(out, "%s[", e->as.array_access.arrayName);
            emit_expr(e->as.array_access.index, out, fstn);
            fprintf(out, "]");
            break;
        }
        

    }
}


void emit_assign_expr_to_var(Expr* e, const char* targetVar, Ownership o, FILE* out, int indent, FuncSignToName* fstn) {
    if (e->type == MATCH_E) {
        int defaultIdx = -1;
        bool firstCondition = true;

        //find wildcard
        for (int i = 0; i < e->as.match.branchCount; i++) {
            if (e->as.match.branches[i].pattern->type == WILDCARD_PATTERN) {
                defaultIdx = i;
                break;
            }
        }

        for (int i = 0; i < e->as.match.branchCount; i++) {
            if (i == defaultIdx) continue;  //handle wildcard at end

            MatchBranchExpr* branch = &e->as.match.branches[i];

            emit_indent(out, indent);
            if (firstCondition) {
                fprintf(out, "if (");
                firstCondition = false;
            } else {
                fprintf(out, "else if (");
            }

            emit_pattern_condition(branch->pattern, e->as.match.var, out, fstn);
            fprintf(out, ") {\n");

            //if SOME_PATTERN, declare binding variable
            if (branch->pattern->type == SOME_PATTERN) {
                emit_indent(out, indent + 1);
                // VAR_T binding -> typed pointer to the source's
                // analyzed_type_name. Same fix as MATCH_S; without it
                // the binding is `void*` and field access fails.
                if (branch->analyzed_type == VAR_T &&
                    e->as.match.var->analyzed_type_name) {
                    fprintf(out, "%s", e->as.match.var->analyzed_type_name);
                } else {
                    emit_type(branch->analyzed_type, out);
                }
                fprintf(out, "* %s = ", branch->pattern->as.binding_name);
                //emit just the variable name, not dereferenced
                if (e->as.match.var->type == VAR_E) {
                    fprintf(out, "%s", e->as.match.var->as.var.name);
                } else {
                    emit_expr(e->as.match.var, out, fstn);
                }
                fprintf(out, ";\n");
            }

            //handles nested matches or simple values
            emit_assign_expr_to_var(branch->caseRet, targetVar, o, out, indent + 1, fstn);

            emit_indent(out, indent);
            fprintf(out, "}\n");
        }

        if (defaultIdx != -1) {
            emit_indent(out, indent);
            fprintf(out, "else {\n");
            emit_assign_expr_to_var(e->as.match.branches[defaultIdx].caseRet, targetVar, o, out, indent + 1, fstn);
            emit_indent(out, indent);
            fprintf(out, "}\n");
        }
    } else if (e->type == ALLOC_E) {
        //reassignment with alloc
        emit_indent(out, indent);
        fprintf(out, "%s = malloc(sizeof(%s));\n", targetVar, type_to_c_type(e->as.alloc.type));
        emit_indent(out, indent);
        fprintf(out, "*%s = ", targetVar);
        emit_expr(e->as.alloc.initialValue, out, fstn);
        fprintf(out, ";\n");
    } else {
        //base case: just a normal assignment
        emit_indent(out, indent);
        bool add_ampersand = (o != OWNERSHIP_NONE && e->type == VAR_E && e->as.var.ownership != OWNERSHIP_NONE);

        //dont dereference if expression is nullable (returns a pointer)
        bool needs_deref = (o != OWNERSHIP_NONE && e->analyzedType != NULL_LIT_T && !e->is_nullable && (e->type == VAR_E ? e->as.var.ownership == OWNERSHIP_NONE : true));

        fprintf(out, "%s%s = %s", needs_deref ? "*" : "", targetVar, add_ampersand ? "&" : "");
        emit_expr(e, out, fstn);
        fprintf(out, ";\n");
    }
}

//emit a statement (with indentation and newlines)
void emit_stmt(Stmt* s, FILE* out, int indent, FuncSignToName* fstn) {
    if (s == NULL) return;

    stage_trace(STAGE_CODEGEN, "emit_stmt: type=%d, indent=%d", s->type, indent);

    switch (s->type) {
        case VAR_DECL_S:
            // fn-typed local: emit `R (*name)(P,P) = init;`. Initializer
            // is a Var of FN_T whose emit will produce the C function name.
            if (s->as.var_decl.varType == FN_T && s->as.var_decl.fnSig) {
                emit_indent(out, indent);
                emit_fn_ptr_decl(s->as.var_decl.fnSig,
                                  s->as.var_decl.name, out);
                fprintf(out, " = ");
                emit_expr(s->as.var_decl.expr, out, fstn);
                fprintf(out, ";\n");
                break;
            }
            // Struct-typed local: emit `TypeName name = <rhs>;`. When the
            // user wrote `p: Point = Point{x:1, y:2};` we hand the RHS
            // expression to emit_expr (typically a STRUCT_LIT_E producing
            // a designated-initializer compound literal). When no RHS was
            // supplied (`p: Point;`), default-zero-init the whole thing.
            // typedef'd name from generate_code is the C type.
            if (s->as.var_decl.varType == VAR_T) {
                // `gm: ref T = f()` should emit `T* gm = f()` (and same for
                // `own T` / nullable VAR_T). The previous fall-through always
                // emitted `T name` by value, which then fails to compile when
                // the RHS is a function returning T*. The default `{0}`
                // initializer for "no RHS" still works for the by-value path
                // since ownership=NONE entities default-zero-init fine.
                const bool is_ptr =
                    (s->as.var_decl.ownership == OWNERSHIP_OWN) ||
                    (s->as.var_decl.ownership == OWNERSHIP_REF) ||
                    s->as.var_decl.isNullable;
                emit_indent(out, indent);
                fprintf(out, "%s%s %s = ",
                        s->as.var_decl.typeName,
                        is_ptr ? "*" : "",
                        s->as.var_decl.name);
                // The parser inserts a VOID_E placeholder for `p: Point;`
                // (no RHS). Treat that as default-zero-init.
                if (s->as.var_decl.expr && s->as.var_decl.expr->type != VOID_E) {
                    emit_expr(s->as.var_decl.expr, out, fstn);
                } else {
                    fprintf(out, is_ptr ? "NULL" : "{0}");
                }
                fprintf(out, ";\n");
                break;
            }
            if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_NONE && s->as.var_decl.elementOwnership == OWNERSHIP_NONE) {
                //case 1: stack array of values - int arr[5]
                emit_indent(out, indent);
                fprintf(out, "%s %s[",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name);
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, "]");

                if (s->as.var_decl.expr->type == ARRAY_DECL_E) {
                    fprintf(out, " = ");
                    emit_expr(s->as.var_decl.expr, out, fstn);
                }
                fprintf(out, ";\n");
            } else if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_OWN && s->as.var_decl.elementOwnership == OWNERSHIP_NONE) {
                //case 3: heap array of values - int* arr = malloc(N * sizeof(int))
                emit_indent(out, indent);
                fprintf(out, "%s* %s = malloc(sizeof(%s) * ",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name,
                        type_to_c_type(s->as.var_decl.varType));
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, ");\n");
            } else if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_NONE && s->as.var_decl.elementOwnership == OWNERSHIP_OWN) {
                //case 4: stack array of owned pointers - int* arr[5]
                emit_indent(out, indent);
                fprintf(out, "%s* %s[",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name);
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, "];\n");
            } else if (s->as.var_decl.isArray && s->as.var_decl.ownership == OWNERSHIP_OWN && s->as.var_decl.elementOwnership == OWNERSHIP_OWN) {
                //case 5: heap array of owned pointers - int** arr = malloc(N * sizeof(int*))
                emit_indent(out, indent);
                fprintf(out, "%s** %s = malloc(sizeof(%s*) * ",
                        type_to_c_type(s->as.var_decl.varType),
                        s->as.var_decl.name,
                        type_to_c_type(s->as.var_decl.varType));
                emit_expr(s->as.var_decl.arraySize, out, fstn);
                fprintf(out, ");\n");
            } else if (s->as.var_decl.expr->type == ALLOC_E) {
                //regular alloc (non-array variable)
                bool isString = (s->as.var_decl.varType == STR_KEYWORD_T);
                
                emit_indent(out, indent);
                fprintf(out, "%s", type_to_c_type(s->as.var_decl.varType));
                //strings in Lync are char*, and own string is just char* (with ownership semantics), so no extra *
                fprintf(out, " %s%s", (s->as.var_decl.ownership != OWNERSHIP_NONE && !isString) ? "*" : "", s->as.var_decl.name);
                
                if (isString && s->as.var_decl.expr->as.alloc.isArray) {
                    //own string = alloc[n] char
                    //char* s = malloc(sizeof(char) * size);
                    fprintf(out, " = malloc(sizeof(%s) * ", type_to_c_type(s->as.var_decl.expr->as.alloc.type));
                    emit_expr(s->as.var_decl.expr->as.alloc.initialValue, out, fstn);
                    fprintf(out, ");\n");
                } else {
                     //own int = alloc 42
                     fprintf(out, " = malloc(sizeof(%s));\n", type_to_c_type(s->as.var_decl.varType));
                     
                     if (s->as.var_decl.expr->as.alloc.isArray) {
                        //allocating an array for a scalar pointer (e.g. string)
                        emit_indent(out, indent);
                        fprintf(out, "*%s = malloc(sizeof(%s) * ", s->as.var_decl.name, type_to_c_type(s->as.var_decl.expr->as.alloc.type));
                         emit_expr(s->as.var_decl.expr->as.alloc.initialValue, out, fstn);
                        fprintf(out, ");\n");
                    } else {
                        emit_assign_expr_to_var(s->as.var_decl.expr->as.alloc.initialValue,
                                                s->as.var_decl.name,
                                                s->as.var_decl.ownership,
                                                out,
                                                indent, fstn);
                    }
                }
            } else {
                //normal variable
                emit_indent(out, indent);
                fprintf(out, "%s", type_to_c_type(s->as.var_decl.varType));
                fprintf(out, " %s%s", s->as.var_decl.ownership != OWNERSHIP_NONE ? "*" : "", s->as.var_decl.name);
                fprintf(out, ";\n");
                emit_assign_expr_to_var(s->as.var_decl.expr,
                                        s->as.var_decl.name,
                                        s->as.var_decl.ownership,
                                        out,
                                        indent, fstn);
            }
            break;

        case ASSIGN_S:
            if (s->as.var_assign.isArray && s->as.var_assign.expr->type == ALLOC_E) {
                //array reallocation
                emit_indent(out, indent);
                fprintf(out, "%s = malloc(sizeof(%s) * ",
                        s->as.var_assign.name,
                        type_to_c_type(s->as.var_assign.expr->as.alloc.type));
                emit_expr(s->as.var_assign.expr->as.alloc.initialValue, out, fstn);
                fprintf(out, ");\n");
            } else {
                emit_assign_expr_to_var(s->as.var_assign.expr, s->as.var_assign.name, s->as.var_assign.ownership, out, indent, fstn);
            }
            break;

        case ARRAY_ELEM_ASSIGN_S:
            emit_indent(out, indent);
            fprintf(out, "%s[", s->as.array_elem_assign.arrayName);
            emit_expr(s->as.array_elem_assign.index, out, fstn);
            fprintf(out, "] = ");
            emit_expr(s->as.array_elem_assign.value, out, fstn);
            fprintf(out, ";\n");
            break;

        case FIELD_ASSIGN_S:
            // target.field = value;  Emit `.` for value-typed targets,
            // `->` for pointer targets (analyzer-set target_is_ptr flag
            // -- nullable VAR_T param, function returning T*, etc).
            //
            // Same auto-deref suppression as FIELD_ACCESS_E: `->` already
            // dereferences, so don't let VAR_E's owned-var emit prepend
            // a stray `*`.
            emit_indent(out, indent);
            if (s->as.field_assign.target_is_ptr
                && s->as.field_assign.target->type == VAR_E) {
                fprintf(out, "%s", s->as.field_assign.target->as.var.name);
            } else {
                emit_expr(s->as.field_assign.target, out, fstn);
            }
            fprintf(out, "%s%s = ",
                s->as.field_assign.target_is_ptr ? "->" : ".",
                s->as.field_assign.field_name);
            emit_expr(s->as.field_assign.value, out, fstn);
            fprintf(out, ";\n");
            break;

        case IF_S:
            emit_indent(out, indent);
            fprintf(out, "if (");
            emit_expr(s->as.if_stmt.cond, out, fstn);
            fprintf(out, ") ");

            //true
            if (s->as.if_stmt.trueStmt->type == BLOCK_S) {
                emit_stmt(s->as.if_stmt.trueStmt, out, indent, fstn);
            } else {
                fprintf(out, "{\n");
                emit_stmt(s->as.if_stmt.trueStmt, out, indent + 1, fstn);
                emit_indent(out, indent);
                fprintf(out, "}");
            }

            //false
            if (s->as.if_stmt.falseStmt != NULL) {
                fprintf(out, " else ");
                if (s->as.if_stmt.falseStmt->type == BLOCK_S) {
                    emit_stmt(s->as.if_stmt.falseStmt, out, indent, fstn);
                } else {
                    fprintf(out, "{\n");
                    emit_stmt(s->as.if_stmt.falseStmt, out, indent + 1, fstn);
                    emit_indent(out, indent);
                    fprintf(out, "}");
                }
            }
            fprintf(out, "\n");
            break;

        case WHILE_S:
            emit_indent(out, indent);
            fprintf(out, "while (");
            emit_expr(s->as.while_stmt.cond, out, fstn);
            fprintf(out, ") ");
            emit_stmt(s->as.while_stmt.body, out, indent, fstn);
            break;

        case DO_WHILE_S:
            emit_indent(out, indent);
            fprintf(out, "do ");
            emit_stmt(s->as.do_while_stmt.body, out, indent, fstn);
            emit_indent(out, indent);
            fprintf(out, "while (");
            emit_expr(s->as.do_while_stmt.cond, out, fstn);
            fprintf(out, ");\n");
            break;

        case FOR_S: {
            const char* lab = s->as.for_stmt.label;
            emit_indent(out, indent);
            fprintf(out, "for (int %s = ", s->as.for_stmt.varName);
            emit_expr(s->as.for_stmt.min, out, fstn);
            fprintf(out, "; %s <= ", s->as.for_stmt.varName);
            emit_expr(s->as.for_stmt.max, out, fstn);
            fprintf(out, "; %s++) {\n", s->as.for_stmt.varName);
            // Body opens its own block. Labelled loops emit two goto
            // targets: `__lync_cont_<label>` at the END of the body
            // (so `continue <label>` skips to the next iteration via
            // the natural for-step), and `__lync_after_<label>` AFTER
            // the for-loop (so `break <label>` jumps past it).
            if (s->as.for_stmt.body && s->as.for_stmt.body->type == BLOCK_S) {
                Stmt* body = s->as.for_stmt.body;
                for (int i = 0; i < body->as.block_stmt.count; i++)
                    emit_stmt(body->as.block_stmt.stmts[i], out, indent + 1, fstn);
            } else {
                emit_stmt(s->as.for_stmt.body, out, indent + 1, fstn);
            }
            if (lab) {
                emit_indent(out, indent + 1);
                fprintf(out, "__lync_cont_%s:;\n", lab);
            }
            emit_indent(out, indent);
            fprintf(out, "}\n");
            if (lab) {
                emit_indent(out, indent);
                fprintf(out, "__lync_after_%s:;\n", lab);
            }
            break;
        }

        case BREAK_S:
            emit_indent(out, indent);
            if (s->as.break_stmt.label)
                fprintf(out, "goto __lync_after_%s;\n", s->as.break_stmt.label);
            else
                fprintf(out, "break;\n");
            break;

        case CONTINUE_S:
            emit_indent(out, indent);
            if (s->as.continue_stmt.label)
                fprintf(out, "goto __lync_cont_%s;\n", s->as.continue_stmt.label);
            else
                fprintf(out, "continue;\n");
            break;

        case BLOCK_S:
            emit_indent(out, indent);
            fprintf(out, "{\n");
            for (int i = 0; i < s->as.block_stmt.count; i++) {
                emit_stmt(s->as.block_stmt.stmts[i], out, indent + 1, fstn);
            }
            emit_indent(out, indent);
            fprintf(out, "}\n");
            break;

        case EXPR_STMT_S:
            // Defer interception: rewrite `return X` to stash the value
            // into __zues_ret and jump to the cleanup label that emits
            // every defer in LIFO before the actual return.
            if (g_func_has_defers &&
                s->as.expr_stmt && s->as.expr_stmt->type == FUNC_RET_E) {
                Expr* ret_expr = s->as.expr_stmt->as.func_ret_expr;
                if (ret_expr && ret_expr->type != VOID_E) {
                    emit_indent(out, indent);
                    fprintf(out, "__zues_ret = ");
                    emit_expr(ret_expr, out, fstn);
                    fprintf(out, ";\n");
                }
                emit_indent(out, indent);
                fprintf(out, "goto __zues_cleanup;\n");
                break;
            }
            emit_indent(out, indent);
            emit_expr(s->as.expr_stmt, out, fstn);
            fprintf(out, ";\n");
            break;

        case DEFER_S:
            // Pre-walked into g_defers + emitted at the cleanup label.
            // Skip here so the body doesn't run inline.
            break;

        case MATCH_S: {
            int wildcardIdx = -1;

            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                if (s->as.match_stmt.branches[i].pattern->type == WILDCARD_PATTERN) {
                    wildcardIdx = i;
                    break;
                }
            }

            bool firstCondition = true;
            for (int i = 0; i < s->as.match_stmt.branchCount; i++) {
                if (i == wildcardIdx) continue;  //handle wildcard at end

                MatchBranchStmt* branch = &s->as.match_stmt.branches[i];

                emit_indent(out, indent);
                if (firstCondition) {
                    fprintf(out, "if (");
                    firstCondition = false;
                } else {
                    fprintf(out, "else if (");
                }

                emit_pattern_condition(branch->pattern, s->as.match_stmt.var, out, fstn);
                fprintf(out, ") {\n");

                if (branch->pattern->type == SOME_PATTERN) {
                    emit_indent(out, indent + 1);
                    // For VAR_T bindings (struct types), emit the
                    // analyzed type name so the resulting C decl is
                    // `MyStruct* x = ...` rather than `void* x = ...` --
                    // otherwise field access through `x` later fails
                    // with "request for member in something not a
                    // structure or union".
                    if (branch->analyzed_type == VAR_T &&
                        s->as.match_stmt.var->analyzed_type_name) {
                        fprintf(out, "%s",
                            s->as.match_stmt.var->analyzed_type_name);
                    } else {
                        emit_type(branch->analyzed_type, out);
                    }
                    fprintf(out, "* %s = ", branch->pattern->as.binding_name);
                    //emit just the variable name, not dereferenced
                    if (s->as.match_stmt.var->type == VAR_E) {
                        fprintf(out, "%s", s->as.match_stmt.var->as.var.name);
                    } else {
                        emit_expr(s->as.match_stmt.var, out, fstn);
                    }
                    fprintf(out, ";\n");
                }

                for (int j = 0; j < branch->stmtCount; j++) {
                    emit_stmt(branch->stmts[j], out, indent + 1, fstn);
                }

                emit_indent(out, indent);
                fprintf(out, "}\n");
            }

            if (wildcardIdx != -1) {
                emit_indent(out, indent);
                fprintf(out, "else {\n");
                MatchBranchStmt* branch = &s->as.match_stmt.branches[wildcardIdx];
                for (int j = 0; j < branch->stmtCount; j++) {
                    emit_stmt(branch->stmts[j], out, indent + 1, fstn);
                }
                emit_indent(out, indent);
                fprintf(out, "}\n");
            }

            break;
        }

        case FREE_S: {
            //for arrays of owned pointers, free each element first
            //we store element ownership info via the analyzed symbol
            //the codegen needs to check the symbol table, so we pass it via free_stmt
            if (s->as.free_stmt.isArrayOfOwned) {
                emit_indent(out, indent);
                fprintf(out, "for (int _i = 0; _i < %d; _i++) {\n", s->as.free_stmt.arraySize);
                emit_indent(out, indent + 1);
                fprintf(out, "free(%s[_i]);\n", s->as.free_stmt.varName);
                emit_indent(out, indent);
                fprintf(out, "}\n");
            }
            emit_indent(out, indent);
            // Translate "l.items" to either "l->items" (when base is a
            // pointer in C; analyzer set target_is_ptr) or "l.items"
            // (value base). Bare identifiers ("p") emit unchanged.
            const char* nm = s->as.free_stmt.varName;
            const char* dot = strchr(nm, '.');
            if (dot) {
                fprintf(out, "free(%.*s%s%s);\n",
                    (int)(dot - nm), nm,
                    s->as.free_stmt.target_is_ptr ? "->" : ".",
                    dot + 1);
            } else {
                fprintf(out, "free(%s);\n", nm);
            }
            break;
        }
    }
}

//main codegen entry point
void generate_code(Program* prog, FILE* output) {
    stage_trace(STAGE_CODEGEN, "generate_code called with prog=%p, output=%p", prog, output);

    if (!prog) {
        stage_fatal(STAGE_CODEGEN, NO_LOC, "Program pointer is NULL");
    }
    if (!output) {
        stage_fatal(STAGE_CODEGEN, NO_LOC, "Output file pointer is NULL");
    }

    stage_trace(STAGE_CODEGEN, "prog->func_count=%d", prog->func_count);

    //emit C headers
    fprintf(output, "#include <stdio.h>\n");
    fprintf(output, "#include <stdlib.h>\n");
    fprintf(output, "#include <stdint.h>\n");
    fprintf(output, "#include <stdbool.h>\n");
    fprintf(output, "#include <stddef.h>\n");   // size_t for `usize` Lync type
    fprintf(output, "#include <string.h>\n");

    //emit extern includes
    // For each extern block: emit the #include AND a C forward declaration
    // for every function the user declared inside. Without the forward
    // decls, calls to those functions trigger "implicit-function-declaration"
    // warnings (and on strict C compilers, errors). The include alone isn't
    // enough — extern fns may live in a TU that ISN'T in the included
    // header (e.g. plugin-emitted Zues wrappers in the same .c file).
    // Headers we already auto-include above. If a user extern block targets
    // one of these, the real prototype is already visible to the C compiler;
    // emitting our own `extern <ret> <name>(<lync-typed-params>)` would just
    // produce a conflicting-types error (e.g. our `malloc(int)` vs libc's
    // `malloc(size_t)`). Suppress the forward-decls in that case and rely on
    // the header's prototype.
    // Standard libc headers whose prototypes use size_t / double / etc. that
    // don't match Lync's int/float exactly. Re-declaring those functions
    // with Lync types after the real header has been included produces a
    // conflicting-types error; trust the header instead.
    static const char* k_auto_headers[] = {
        "stdio.h", "stdlib.h", "stdint.h", "stdbool.h", "string.h",
        "math.h", "ctype.h", "time.h", "errno.h", "stddef.h", "assert.h",
        "limits.h", "float.h", "wchar.h", "wctype.h", "locale.h", "signal.h",
        "setjmp.h", NULL
    };
    // Phase 1: Emit ONLY the #include directives from extern blocks. The
    // forward-decl pass below comes AFTER struct typedefs so extern
    // signatures referencing user-declared structs (e.g.
    // `def KeyCode(): KeyCodeT;` -> `extern KeyCodeT KeyCode(void);`) see
    // the typedef and don't fall back to implicit-int.
    for(int i = 0; i < prog->ext_block_count; ++i) {
        ExternBlock* eb = prog->externBlocks[i];
        fprintf(output, "#include <%s>\n", eb->header);
    }

    //add platform-specific headers for read_key if needed
    if (prog->imports && prog->imports->import_count > 0) {
        bool need_read_key = false;
        for (int i = 0; i < prog->imports->import_count; i++) {
            IncludeStmt* import = prog->imports->imports[i];
            if (import->type == IMPORT_ALL && strcmp(import->module_name, "std.io") == 0) {
                need_read_key = true;
                break;
            } else if (import->type == IMPORT_SPECIFIC && strcmp(import->function_name, "read_key") == 0) {
                need_read_key = true;
                break;
            }
        }
        if (need_read_key) {
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "#include <conio.h>\n");
            fprintf(output, "#else\n");
            fprintf(output, "#include <termios.h>\n");
            fprintf(output, "#include <unistd.h>\n");
            fprintf(output, "#endif\n");
        }
    }

    fprintf(output, "\n");

    stage_trace(STAGE_CODEGEN, "headers written, checking imports");

    //generate C helper functions based on imports
    if (prog->imports && prog->imports->import_count > 0) {
        fprintf(output, "//std.io helper functions\n");

        //check which functions are imported
        bool has_wildcard = false;
        bool need_read_int = false;
        bool need_read_str = false;
        bool need_read_bool = false;
        bool need_read_char = false;
        bool need_read_float = false;
        bool need_read_double = false;
        bool need_read_key = false;

        for (int i = 0; i < prog->imports->import_count; i++) {
            IncludeStmt* import = prog->imports->imports[i];
            if (import->type == IMPORT_ALL && strcmp(import->module_name, "std.io") == 0) {
                has_wildcard = true;
                break;
            } else if (import->type == IMPORT_SPECIFIC) {
                if (strcmp(import->function_name, "read_int") == 0) need_read_int = true;
                else if (strcmp(import->function_name, "read_str") == 0) need_read_str = true;
                else if (strcmp(import->function_name, "read_bool") == 0) need_read_bool = true;
                else if (strcmp(import->function_name, "read_char") == 0) need_read_char = true;
                else if (strcmp(import->function_name, "read_float") == 0) need_read_float = true;
                else if (strcmp(import->function_name, "read_double") == 0) need_read_double = true;
                else if (strcmp(import->function_name, "read_key") == 0) need_read_key = true;
            }
        }

        //generate read_int
        if (has_wildcard || need_read_int) {
            fprintf(output, "int* read_int() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    int* result = malloc(sizeof(int));\n");
            fprintf(output, "    *result = atoll(buffer);\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_str
        if (has_wildcard || need_read_str) {
            fprintf(output, "char** read_str() {\n");
            fprintf(output, "    char buffer[1024];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    //remove trailing newline\n");
            fprintf(output, "    size_t len = strlen(buffer);\n");
            fprintf(output, "    if (len > 0 && buffer[len-1] == '\\n') buffer[len-1] = '\\0';\n");
            fprintf(output, "    char** result = malloc(sizeof(char*));\n");
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "    *result = _strdup(buffer);\n");
            fprintf(output, "#else\n");
            fprintf(output, "    *result = strdup(buffer);\n");
            fprintf(output, "#endif\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_bool
        if (has_wildcard || need_read_bool) {
            fprintf(output, "bool* read_bool() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    bool* result = malloc(sizeof(bool));\n");
            fprintf(output, "    if (strncmp(buffer, \"true\", 4) == 0 || strncmp(buffer, \"1\", 1) == 0) {\n");
            fprintf(output, "        *result = true;\n");
            fprintf(output, "    } else if (strncmp(buffer, \"false\", 5) == 0 || strncmp(buffer, \"0\", 1) == 0) {\n");
            fprintf(output, "        *result = false;\n");
            fprintf(output, "    } else {\n");
            fprintf(output, "        free(result);\n");
            fprintf(output, "        return NULL;\n");
            fprintf(output, "    }\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_char
        if (has_wildcard || need_read_char) {
            fprintf(output, "char* read_char() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    if (buffer[0] == '\\0' || buffer[0] == '\\n') return NULL;\n");
            fprintf(output, "    char* result = malloc(sizeof(char));\n");
            fprintf(output, "    *result = buffer[0];\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_float
        if (has_wildcard || need_read_float) {
            fprintf(output, "float* read_float() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    float* result = malloc(sizeof(float));\n");
            fprintf(output, "    *result = strtof(buffer, NULL);\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_double
        if (has_wildcard || need_read_double) {
            fprintf(output, "double* read_double() {\n");
            fprintf(output, "    char buffer[256];\n");
            fprintf(output, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) return NULL;\n");
            fprintf(output, "    double* result = malloc(sizeof(double));\n");
            fprintf(output, "    *result = strtod(buffer, NULL);\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }

        //generate read_key (tbh no idea how this works but oh well, not all code needs to be mine :D)
        if (has_wildcard || need_read_key) {
            fprintf(output, "char* read_key() {\n");
            fprintf(output, "    char* result = malloc(sizeof(char));\n");
            fprintf(output, "#ifdef _WIN32\n");
            fprintf(output, "    *result = _getch();\n");
            fprintf(output, "#else\n");
            fprintf(output, "    struct termios oldt, newt;\n");
            fprintf(output, "    tcgetattr(STDIN_FILENO, &oldt);\n");
            fprintf(output, "    newt = oldt;\n");
            fprintf(output, "    newt.c_lflag &= ~(ICANON | ECHO);\n");
            fprintf(output, "    tcsetattr(STDIN_FILENO, TCSANOW, &newt);\n");
            fprintf(output, "    *result = getchar();\n");
            fprintf(output, "    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);\n");
            fprintf(output, "#endif\n");
            fprintf(output, "    return result;\n");
            fprintf(output, "}\n\n");
        }
    }

    stage_trace(STAGE_CODEGEN, "imports processed, allocating FuncSignToName");

    // Emit struct typedefs BEFORE function declarations so functions can
    // take/return struct types. Order in source = order of emission;
    // forward refs across structs would need a topological sort here, but
    // we already validate "struct field of unknown type" in the analyzer
    // so any well-formed program orders correctly by user discipline.
    for (int i = 0; i < prog->struct_count; ++i) {
        StructDecl* d = prog->structs[i];
        if (d && d->type_params) continue;     // skip raw templates; only emit monomorphisations
        emit_attr_list(d->attrs, output);
        fprintf(output, "typedef struct {\n");
        for (int j = 0; j < d->field_count; ++j) {
            StructField* f = &d->fields[j];
            const char* c_type = (f->type == VAR_T)
                ? f->type_name              // user struct - typedef matches name
                : type_to_c_type(f->type);  // primitive
            // own/ref fields become C pointers. ptr is already void*
            // so adding another `*` would yield void**; skip the suffix
            // for STR_KEYWORD_T too since that's already char*.
            const char* ptr_suffix =
                (f->ownership != OWNERSHIP_NONE &&
                 f->type != PTR_KEYWORD_T &&
                 f->type != STR_KEYWORD_T) ? "*" : "";
            fprintf(output, "    %s%s %s;\n", c_type, ptr_suffix, f->name);
        }
        fprintf(output, "} %s;\n\n", d->name);
    }

    // Phase 2: Extern forward declarations. Emitted AFTER struct typedefs
    // so signatures that reference user structs (return or param) see the
    // typedef and don't fall back to C's implicit-int rule. Headers that
    // we already auto-include (stdio/stdlib/...) are skipped because the
    // real prototypes come from the system header and wouldn't match
    // lync's lower-fidelity types (e.g. malloc(int) vs malloc(size_t)).
    for(int i = 0; i < prog->ext_block_count; ++i) {
        ExternBlock* eb = prog->externBlocks[i];
        bool auto_included = false;
        for (int k = 0; k_auto_headers[k]; ++k) {
            if (strcmp(eb->header, k_auto_headers[k]) == 0) {
                auto_included = true;
                break;
            }
        }
        if (auto_included) continue;
        for (int j = 0; j < eb->count; ++j) {
            FuncSign* sig = eb->signs[j];
            const char* ret_c = (sig->retType == VAR_T && sig->retTypeName)
                ? sig->retTypeName
                : type_to_c_type(sig->retType);
            // Pointer-suffix the return type when the extern declares any
            // ownership (own/ref) on a non-string scalar/struct, OR when the
            // return is a nullable VAR_T (legacy `: T?` shape). Both
            // ownership flavours map to the same C `T*`.
            const char* ptr_suffix =
                ((sig->retOwnership != OWNERSHIP_NONE && sig->retType != STR_KEYWORD_T)
                 || (sig->retNullable && sig->retType == VAR_T)) ? "*" : "";
            fprintf(output, "extern %s%s %s(", ret_c, ptr_suffix, sig->name);
            if (sig->paramNum == 0) {
                fprintf(output, "void");
            } else {
                for (int p = 0; p < sig->paramNum; ++p) {
                    if (p > 0) fprintf(output, ", ");
                    FuncParam* fp = &sig->parameters[p];
                    const char* pc = (fp->type == VAR_T && fp->type_name)
                        ? fp->type_name
                        : type_to_c_type(fp->type);
                    // Same pointer-suffix rule for params: any ownership
                    // marker (own/ref) or nullable VAR_T -> T*.
                    const bool param_ptr =
                        (fp->ownership != OWNERSHIP_NONE && fp->type != STR_KEYWORD_T)
                        || (fp->isNullable && fp->type == VAR_T);
                    fprintf(output, "%s%s", pc, param_ptr ? "*" : "");
                }
            }
            fprintf(output, ");\n");
        }
    }

    FuncSignToName* fstn = malloc(sizeof(FuncSignToName));
    fstn->count = 0;
    fstn->height = 2;
    fstn->elements = malloc(sizeof(FuncSignToNameElement) * fstn->height);

    FuncNameCounter* fnc = malloc(sizeof(FuncNameCounter));
    fnc->count = 0;
    fnc->height = 2;
    fnc->elements = malloc(sizeof(FuncSignToNameElement) * fnc->height);

    Func** program = prog->functions;
    int count = prog->func_count;

    stage_trace(STAGE_CODEGEN, "emitting %d function declarations", count);

    for (int i = 0; i < count; ++i) {
        if (program[i] && program[i]->type_params) continue;   // skip raw templates
        stage_trace(STAGE_CODEGEN, "emitting decl for function %d", i);
        emit_func_decl(program[i], output, fnc, fstn);
    }

    stage_trace(STAGE_CODEGEN, "emitting %d function definitions", count);

    for (int i = 0; i < count; ++i) {
        if (program[i] && program[i]->type_params) continue;
        stage_trace(STAGE_CODEGEN, "emitting function %d", i);
        emit_func(program[i], output, fstn);
    }

    stage_trace(STAGE_CODEGEN, "all functions emitted, cleaning up");

    free(fstn->elements);
    free(fstn);
    free(fnc->elements);
    free(fnc);
}