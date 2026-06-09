// template.c — Lync template (compile-time monomorphisation) support.
//
// Templates are C++-style: parse the body once with type-parameter tokens
// in place, then at every use site clone the AST, substitute the params for
// concrete types, and register the result under a mangled name. The
// analyzer + codegen never see a template — by the time they run, every
// reference has been resolved to a concrete monomorphised decl.
//
// Public surface:
//   - tpl_mangle(name, args)               -> "name__t1_t2"
//   - tpl_push_pending(prog, ...)          -> queue an instantiation
//   - tpl_already_instantiated(prog, name) -> memo check
//   - tpl_drain_pending(prog)              -> realize all queued templates
//
// All other helpers (clone_*, subst_*) are internal to this file.

#include "parser.h"
#include "lexer.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------------
// Misc helpers
// ----------------------------------------------------------------------------

static char* x_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* o = malloc(n + 1);
    memcpy(o, s, n + 1);
    return o;
}

static char* x_strndup(const char* s, size_t n) {
    if (!s) return NULL;
    char* o = malloc(n + 1);
    memcpy(o, s, n);
    o[n] = 0;
    return o;
}

// Stringify a TypeArg for the mangled name. Primitive TokenType -> short
// keyword; struct (VAR_T) -> the struct name verbatim. Keeps the mangled
// name predictable and grep-friendly.
static const char* type_arg_text(const TypeArg* a) {
    if (a->type == VAR_T) return a->type_name;
    switch (a->type) {
        case INT_KEYWORD_T:    return "int";
        case BOOL_KEYWORD_T:   return "bool";
        case STR_KEYWORD_T:    return "string";
        case CHAR_KEYWORD_T:   return "char";
        case FLOAT_KEYWORD_T:  return "float";
        case DOUBLE_KEYWORD_T: return "double";
        case PTR_KEYWORD_T:    return "ptr";
        case VOID_KEYWORD_T:   return "void";
        default:               return "unk";
    }
}

// "List" + [int] -> "List__int"; "Pair" + [int, string] -> "Pair__int_string".
char* tpl_mangle(const char* base_name, const TypeArgList* args) {
    if (!args || args->count == 0) return x_strdup(base_name);
    size_t cap = strlen(base_name) + 3;  // "__"
    for (int i = 0; i < args->count; ++i) {
        cap += strlen(type_arg_text(&args->args[i])) + 1;  // sep
    }
    char* out = malloc(cap + 1);
    int off = snprintf(out, cap + 1, "%s__", base_name);
    for (int i = 0; i < args->count; ++i) {
        if (i > 0) out[off++] = '_';
        off += snprintf(out + off, cap + 1 - off, "%s", type_arg_text(&args->args[i]));
    }
    out[off] = 0;
    return out;
}

// ----------------------------------------------------------------------------
// Pending queue + memo
// ----------------------------------------------------------------------------

// Check if an extern block declares a function with this exact name. Used
// by the plugin-emitted per-component decls (Add__Health, Get__Health, ...)
// so user code like `Add<Health>(e, h)` mangles, finds the extern, and
// links to the host's C symbol without needing an actual template body.
static bool extern_declares(const Program* p, const char* name) {
    if (!p || !name) return false;
    for (int i = 0; i < p->ext_block_count; ++i) {
        ExternBlock* b = p->externBlocks[i];
        if (!b) continue;
        for (int j = 0; j < b->count; ++j) {
            if (b->signs[j] && b->signs[j]->name && strcmp(b->signs[j]->name, name) == 0)
                return true;
        }
    }
    return false;
}

bool tpl_already_instantiated(const Program* p, const char* mangled) {
    if (!p || !mangled) return false;
    for (int i = 0; i < p->instantiated_count; ++i) {
        if (strcmp(p->instantiated_names[i], mangled) == 0) return true;
    }
    // Also count anything currently in the program tables — covers concrete
    // user decls that happen to share a name (defensive).
    for (int i = 0; i < p->struct_count; ++i)
        if (p->structs[i] && strcmp(p->structs[i]->name, mangled) == 0) return true;
    for (int i = 0; i < p->func_count; ++i)
        if (p->functions[i] && p->functions[i]->signature &&
            strcmp(p->functions[i]->signature->name, mangled) == 0) return true;
    if (extern_declares(p, mangled)) return true;
    return false;
}

static void tpl_remember(Program* p, const char* mangled) {
    if (p->instantiated_count >= p->instantiated_capacity) {
        p->instantiated_capacity = p->instantiated_capacity ? p->instantiated_capacity * 2 : 16;
        p->instantiated_names = realloc(p->instantiated_names,
                                        sizeof(char*) * p->instantiated_capacity);
    }
    p->instantiated_names[p->instantiated_count++] = x_strdup(mangled);
}

void tpl_push_pending(Program* p, const char* template_name,
                      TypeArgList* args, const char* mangled,
                      SourceLocation loc, int kind) {
    if (!p || !template_name || !mangled) return;
    if (tpl_already_instantiated(p, mangled)) return;
    // Also de-dup against pending — caller might hit the same use site twice.
    for (int i = 0; i < p->pending_count; ++i) {
        if (strcmp(p->pending[i]->mangled_name, mangled) == 0) return;
    }
    if (p->pending_count >= p->pending_capacity) {
        p->pending_capacity = p->pending_capacity ? p->pending_capacity * 2 : 16;
        p->pending = realloc(p->pending, sizeof(PendingInstantiation*) * p->pending_capacity);
    }
    PendingInstantiation* pi = malloc(sizeof(PendingInstantiation));
    pi->template_name = x_strdup(template_name);
    pi->type_args     = args;        // takes ownership
    pi->mangled_name  = x_strdup(mangled);
    pi->use_loc       = loc;
    pi->kind          = kind;
    p->pending[p->pending_count++] = pi;
}

// ----------------------------------------------------------------------------
// Type-param substitution (the heart of the system).
//
// A template body is just a normal AST where some VAR_T tokens carry the
// type-param name (e.g. "T") in their `type_name` slot. We walk the cloned
// AST, find every type-position reference whose name matches a param, and
// rewrite it in-place to the corresponding concrete TypeArg.
// ----------------------------------------------------------------------------

// Find the index of a type-param name, or -1 if not a param.
static int param_index(const TypeParamList* tp, const char* name) {
    if (!tp || !name) return -1;
    for (int i = 0; i < tp->count; ++i) {
        if (strcmp(tp->names[i], name) == 0) return i;
    }
    return -1;
}

// Set by tpl_drain_pending for the duration of one clone -- subst_type
// uses it to push newly-discovered struct instantiations (e.g. when a
// generic def's parameter type `ref List<T>` instantiates as
// `List__int`, that has to be queued for drain even though only the
// outer template was originally pushed). Cleared after each instantiation.
static Program* g_subst_prog       = NULL;
static SourceLocation g_subst_loc  = {0};

// Render a TypeArg's text the same way tpl_mangle does -- so the
// substituted parts agree byte-for-byte with what the mangler would
// have produced if the name had been concrete from the start.
static const char* subst_type_arg_text(const TypeArg* a) {
    switch (a->type) {
        case INT_KEYWORD_T:    return "int";
        case USIZE_KEYWORD_T:  return "usize";
        case BOOL_KEYWORD_T:   return "bool";
        case CHAR_KEYWORD_T:   return "char";
        case STR_KEYWORD_T:    return "string";
        case FLOAT_KEYWORD_T:  return "float";
        case DOUBLE_KEYWORD_T: return "double";
        case PTR_KEYWORD_T:    return "ptr";
        case VAR_T:            return a->type_name ? a->type_name : "void";
        default:               return "void";
    }
}

// Try to substitute template params inside an already-mangled name like
// "List__T" or "_list_ensure_cap__T". Returns the new x_strdup'd name on
// success (caller frees the old), or NULL if no substitution applied.
// `kind` is the pending-instantiation kind to queue (0 = func, 1 = struct);
// pass -1 to skip the queueing (caller will handle pending separately).
static char* try_subst_mangled(const char* name,
                                const TypeParamList* tp,
                                const TypeArgList* args,
                                int kind) {
    if (!name || !tp || !args || tp->count == 0 || args->count == 0) return NULL;
    const char* sep = strstr(name, "__");
    if (!sep) return NULL;
    const char* args_str = sep + 2;

    // Quick check: do any segments match a template param?
    bool any_substituted = false;
    {
        const char* p = args_str;
        while (*p) {
            const char* q = p;
            while (*q && *q != '_') ++q;
            int len = (int)(q - p);
            for (int i = 0; i < tp->count; ++i) {
                if ((int)strlen(tp->names[i]) == len &&
                    strncmp(tp->names[i], p, len) == 0) {
                    any_substituted = true; break;
                }
            }
            if (any_substituted) break;
            p = (*q == '_') ? q + 1 : q;
        }
    }
    if (!any_substituted) return NULL;

    char buf[256];
    int  off = (int)snprintf(buf, sizeof(buf), "%.*s__",
                              (int)(sep - name), name);
    bool first_arg = true;
    const char* p = args_str;
    while (*p && off < (int)sizeof(buf) - 1) {
        const char* q = p;
        while (*q && *q != '_') ++q;
        int len = (int)(q - p);
        if (!first_arg) buf[off++] = '_';
        first_arg = false;
        const char* repl = NULL;
        for (int i = 0; i < tp->count; ++i) {
            if ((int)strlen(tp->names[i]) == len &&
                strncmp(tp->names[i], p, len) == 0) {
                repl = subst_type_arg_text(&args->args[i]); break;
            }
        }
        if (repl) {
            off += snprintf(buf + off, sizeof(buf) - off, "%s", repl);
        } else {
            off += snprintf(buf + off, sizeof(buf) - off, "%.*s", len, p);
        }
        p = (*q == '_') ? q + 1 : q;
    }
    buf[off] = 0;

    // Queue the substituted instantiation if a program is active.
    if (kind >= 0 && g_subst_prog) {
        int seg_count = 0;
        for (const char* ap = args_str; *ap; ) {
            ++seg_count;
            while (*ap && *ap != '_') ++ap;
            if (*ap == '_') ++ap;
        }
        TypeArgList* new_args = malloc(sizeof(TypeArgList));
        new_args->count = 0;
        new_args->args  = malloc(sizeof(TypeArg) * (seg_count > 0 ? seg_count : 1));
        const char* ap = args_str;
        while (*ap) {
            const char* aq = ap;
            while (*aq && *aq != '_') ++aq;
            int alen = (int)(aq - ap);
            const TypeArg* match = NULL;
            for (int i = 0; i < tp->count; ++i) {
                if ((int)strlen(tp->names[i]) == alen &&
                    strncmp(tp->names[i], ap, alen) == 0) {
                    match = &args->args[i]; break;
                }
            }
            TypeArg* slot = &new_args->args[new_args->count++];
            if (match) {
                *slot = *match;
                if (match->type == VAR_T && match->type_name)
                    slot->type_name = x_strdup(match->type_name);
            } else {
                slot->type = VAR_T;
                slot->type_name = x_strndup(ap, alen);
            }
            ap = (*aq == '_') ? aq + 1 : aq;
        }
        char* base_end = strstr(buf, "__");
        char* base_name = base_end
            ? x_strndup(buf, base_end - buf)
            : x_strdup(buf);
        tpl_push_pending(g_subst_prog, base_name, new_args,
                          x_strdup(buf), g_subst_loc, kind);
        free(base_name);
    }
    return x_strdup(buf);
}

// Substitute one type slot in-place. `type` is the TokenType field; `name`
// is the corresponding type-name string (only meaningful when *type == VAR_T).
// Returns true if a substitution happened.
//
// Two cases both handled here:
//   1) Bare param: `T` -> the matching arg. Direct hit on param_index.
//   2) Mangled embedded use: `List__T` -> `List__int`. This happens when
//      a generic def writes `ref List<T>` for a parameter -- the parser
//      eagerly mangles to `List__T` at parse time. At instantiation we
//      have to walk the mangled segments, substitute any that refer to
//      our template parameters, and re-mangle.
static bool subst_type(TokenType* type, char** name,
                       const TypeParamList* tp, const TypeArgList* args) {
    if (*type != VAR_T || !*name) return false;

    // Case 1: direct param match.
    int idx = param_index(tp, *name);
    if (idx >= 0 && idx < args->count) {
        const TypeArg* a = &args->args[idx];
        *type = a->type;
        if (a->type == VAR_T) {
            *name = x_strdup(a->type_name);
        } else {
            *name = NULL;  // primitive types carry no name
        }
        return true;
    }

    // Case 2: mangled name like "List__T" or "Map__T_U". Delegate to
    // try_subst_mangled which handles the parsing + re-mangling + pending
    // queue (kind=struct since we're substituting a type slot).
    char* sub = try_subst_mangled(*name, tp, args, /*kind=struct*/ 1);
    if (!sub) return false;
    *name = sub;
    *type = VAR_T;
    return true;
}

// Forward decls for the AST walkers.
static Expr* clone_expr(const Expr* src, const TypeParamList* tp, const TypeArgList* args, Program* prog);
static Stmt* clone_stmt(const Stmt* src, const TypeParamList* tp, const TypeArgList* args, Program* prog);

// Walk a FuncSign (parameter list + return) substituting in place.
static void subst_func_sign(FuncSign* s, const TypeParamList* tp, const TypeArgList* args) {
    if (!s) return;
    subst_type(&s->retType, &s->retTypeName, tp, args);
    for (int i = 0; i < s->paramNum; ++i) {
        FuncParam* p = &s->parameters[i];
        subst_type(&p->type, &p->type_name, tp, args);
    }
}

// ----------------------------------------------------------------------------
// Expression cloning. Each branch deep-copies the source node's payload and
// recurses into children. Type fields go through subst_type so any embedded
// `T` becomes a concrete type.
// ----------------------------------------------------------------------------

static Expr** clone_expr_array(Expr** src, int n,
                               const TypeParamList* tp, const TypeArgList* args, Program* prog) {
    if (n <= 0 || !src) return NULL;
    Expr** out = malloc(sizeof(Expr*) * n);
    for (int i = 0; i < n; ++i) out[i] = clone_expr(src[i], tp, args, prog);
    return out;
}

static Expr* clone_expr(const Expr* src, const TypeParamList* tp,
                        const TypeArgList* args, Program* prog) {
    if (!src) return NULL;
    Expr* e = malloc(sizeof(Expr));
    *e = *src;  // start with shallow copy; fix the union members below
    e->analyzed_type_name = NULL;   // reset analyzer scratch on clone
    e->analyzed_fn_sig    = NULL;

    switch (src->type) {
        case INT_LIT_E: case BOOL_LIT_E: case FLOAT_LIT_E: case CHAR_LIT_E:
        case NULL_LIT_E: case VOID_E:
            break;  // POD, the shallow copy was sufficient

        case STR_LIT_E:
            e->as.str_val = x_strdup(src->as.str_val);
            break;

        case VAR_E:
            e->as.var.name = x_strdup(src->as.var.name);
            break;

        case ARRAY_ACCESS_E:
            e->as.array_access.arrayName = x_strdup(src->as.array_access.arrayName);
            e->as.array_access.index     = clone_expr(src->as.array_access.index, tp, args, prog);
            break;

        case UN_OP_E:
            e->as.un_op.expr = clone_expr(src->as.un_op.expr, tp, args, prog);
            break;

        case BIN_OP_E:
            e->as.bin_op.exprL = clone_expr(src->as.bin_op.exprL, tp, args, prog);
            e->as.bin_op.exprR = clone_expr(src->as.bin_op.exprR, tp, args, prog);
            break;

        case FUNC_CALL_E: {
            // The name might itself be a template use (e.g. inside list_push<T>
            // we call _list_ensure_cap<T>). The parser-time mangler has
            // already baked the template-param name into the call name as
            // "_list_ensure_cap__T". try_subst_mangled walks the segments
            // and re-mangles to "_list_ensure_cap__int" for an int
            // instantiation, AND queues that as a new pending function.
            char* subst = try_subst_mangled(src->as.func_call.name, tp, args,
                                             /*kind=func*/ 0);
            e->as.func_call.name = subst ? subst
                                          : x_strdup(src->as.func_call.name);
            e->as.func_call.params = clone_expr_array(src->as.func_call.params,
                                                      src->as.func_call.count, tp, args, prog);
            e->as.func_call.resolved_sign = NULL;
            break;
        }

        case ARRAY_DECL_E:
            e->as.arr_decl.values = clone_expr_array(src->as.arr_decl.values,
                                                     src->as.arr_decl.count, tp, args, prog);
            break;

        case FUNC_RET_E:
            e->as.func_ret_expr = clone_expr(src->as.func_ret_expr, tp, args, prog);
            break;

        case ALLOC_E: case ALLOC_ARR_E:
            e->as.alloc.initialValue = clone_expr(src->as.alloc.initialValue, tp, args, prog);
            // type field may itself be the template param.
            (void)subst_type(&e->as.alloc.type, &(char*){NULL}, tp, args);
            break;

        case MATCH_E: {
            e->as.match.var = clone_expr(src->as.match.var, tp, args, prog);
            int n = src->as.match.branchCount;
            e->as.match.branches = malloc(sizeof(MatchBranchExpr) * n);
            for (int i = 0; i < n; ++i) {
                e->as.match.branches[i] = src->as.match.branches[i];
                e->as.match.branches[i].caseRet = clone_expr(src->as.match.branches[i].caseRet, tp, args, prog);
                if (src->as.match.branches[i].pattern) {
                    Pattern* p = malloc(sizeof(Pattern));
                    *p = *src->as.match.branches[i].pattern;
                    if (p->type == SOME_PATTERN) p->as.binding_name = x_strdup(p->as.binding_name);
                    if (p->type == VALUE_PATTERN) p->as.value_expr = clone_expr(p->as.value_expr, tp, args, prog);
                    e->as.match.branches[i].pattern = p;
                }
            }
            break;
        }

        case SOME_E:
            e->as.some.var = clone_expr(src->as.some.var, tp, args, prog);
            break;

        case STRUCT_LIT_E: {
            e->as.struct_lit.type_name = x_strdup(src->as.struct_lit.type_name);
            // If the struct lit's type is a template param T, the type name
            // becomes the concrete struct name.
            int idx = param_index(tp, e->as.struct_lit.type_name);
            if (idx >= 0 && args->args[idx].type == VAR_T) {
                free(e->as.struct_lit.type_name);
                e->as.struct_lit.type_name = x_strdup(args->args[idx].type_name);
            }
            int n = src->as.struct_lit.field_count;
            e->as.struct_lit.field_names = malloc(sizeof(char*) * n);
            e->as.struct_lit.field_values = malloc(sizeof(Expr*) * n);
            for (int i = 0; i < n; ++i) {
                e->as.struct_lit.field_names[i]  = x_strdup(src->as.struct_lit.field_names[i]);
                e->as.struct_lit.field_values[i] = clone_expr(src->as.struct_lit.field_values[i], tp, args, prog);
            }
            break;
        }

        case FIELD_ACCESS_E:
            e->as.field_access.target          = clone_expr(src->as.field_access.target, tp, args, prog);
            e->as.field_access.field_name      = x_strdup(src->as.field_access.field_name);
            e->as.field_access.field_type_name = NULL;  // analyzer fills
            break;

        case SIZEOF_E: {
            // Substitute the operand type if it's a template parameter
            // (`sizeof(T)` inside a template body becomes `sizeof(int)`
            // at int-instantiation). Reuse subst_type for the
            // primitive/struct split.
            char* name_ptr = src->as.sizeof_op.operand_type_name
                ? x_strdup(src->as.sizeof_op.operand_type_name) : NULL;
            e->as.sizeof_op.operand_type      = src->as.sizeof_op.operand_type;
            e->as.sizeof_op.operand_type_name = name_ptr;
            (void)subst_type(&e->as.sizeof_op.operand_type,
                              &e->as.sizeof_op.operand_type_name, tp, args);
            break;
        }

        case ADDR_OF_E:
            e->as.addr_of.target = clone_expr(src->as.addr_of.target, tp, args, prog);
            break;
    }
    return e;
}

// ----------------------------------------------------------------------------
// Statement cloning.
// ----------------------------------------------------------------------------

static Stmt* clone_stmt(const Stmt* src, const TypeParamList* tp,
                        const TypeArgList* args, Program* prog) {
    if (!src) return NULL;
    Stmt* s = malloc(sizeof(Stmt));
    *s = *src;

    switch (src->type) {
        case VAR_DECL_S:
            s->as.var_decl.name      = x_strdup(src->as.var_decl.name);
            s->as.var_decl.typeName  = src->as.var_decl.typeName ? x_strdup(src->as.var_decl.typeName) : NULL;
            (void)subst_type(&s->as.var_decl.varType, &s->as.var_decl.typeName, tp, args);
            s->as.var_decl.expr      = clone_expr(src->as.var_decl.expr, tp, args, prog);
            s->as.var_decl.arraySize = clone_expr(src->as.var_decl.arraySize, tp, args, prog);
            break;

        case ASSIGN_S:
            s->as.var_assign.name = x_strdup(src->as.var_assign.name);
            s->as.var_assign.expr = clone_expr(src->as.var_assign.expr, tp, args, prog);
            break;

        case ARRAY_ELEM_ASSIGN_S:
            s->as.array_elem_assign.arrayName = x_strdup(src->as.array_elem_assign.arrayName);
            s->as.array_elem_assign.index     = clone_expr(src->as.array_elem_assign.index, tp, args, prog);
            s->as.array_elem_assign.value     = clone_expr(src->as.array_elem_assign.value, tp, args, prog);
            break;

        case FIELD_ASSIGN_S:
            s->as.field_assign.target     = clone_expr(src->as.field_assign.target, tp, args, prog);
            s->as.field_assign.field_name = x_strdup(src->as.field_assign.field_name);
            s->as.field_assign.value      = clone_expr(src->as.field_assign.value, tp, args, prog);
            break;

        case IF_S:
            s->as.if_stmt.cond      = clone_expr(src->as.if_stmt.cond, tp, args, prog);
            s->as.if_stmt.trueStmt  = clone_stmt(src->as.if_stmt.trueStmt, tp, args, prog);
            s->as.if_stmt.falseStmt = clone_stmt(src->as.if_stmt.falseStmt, tp, args, prog);
            break;

        case WHILE_S:
            s->as.while_stmt.cond = clone_expr(src->as.while_stmt.cond, tp, args, prog);
            s->as.while_stmt.body = clone_stmt(src->as.while_stmt.body, tp, args, prog);
            break;

        case DO_WHILE_S:
            s->as.do_while_stmt.cond = clone_expr(src->as.do_while_stmt.cond, tp, args, prog);
            s->as.do_while_stmt.body = clone_stmt(src->as.do_while_stmt.body, tp, args, prog);
            break;

        case FOR_S:
            s->as.for_stmt.varName = x_strdup(src->as.for_stmt.varName);
            s->as.for_stmt.min     = clone_expr(src->as.for_stmt.min, tp, args, prog);
            s->as.for_stmt.max     = clone_expr(src->as.for_stmt.max, tp, args, prog);
            s->as.for_stmt.body    = clone_stmt(src->as.for_stmt.body, tp, args, prog);
            break;

        case BLOCK_S: {
            int n = src->as.block_stmt.count;
            s->as.block_stmt.stmts = malloc(sizeof(Stmt*) * n);
            for (int i = 0; i < n; ++i) s->as.block_stmt.stmts[i] = clone_stmt(src->as.block_stmt.stmts[i], tp, args, prog);
            break;
        }

        case MATCH_S: {
            s->as.match_stmt.var = clone_expr(src->as.match_stmt.var, tp, args, prog);
            int n = src->as.match_stmt.branchCount;
            s->as.match_stmt.branches = malloc(sizeof(MatchBranchStmt) * n);
            for (int i = 0; i < n; ++i) {
                s->as.match_stmt.branches[i] = src->as.match_stmt.branches[i];
                int sn = src->as.match_stmt.branches[i].stmtCount;
                s->as.match_stmt.branches[i].stmts = malloc(sizeof(Stmt*) * sn);
                for (int j = 0; j < sn; ++j) {
                    s->as.match_stmt.branches[i].stmts[j] = clone_stmt(src->as.match_stmt.branches[i].stmts[j], tp, args, prog);
                }
                if (src->as.match_stmt.branches[i].pattern) {
                    Pattern* p = malloc(sizeof(Pattern));
                    *p = *src->as.match_stmt.branches[i].pattern;
                    if (p->type == SOME_PATTERN) p->as.binding_name = x_strdup(p->as.binding_name);
                    if (p->type == VALUE_PATTERN) p->as.value_expr = clone_expr(p->as.value_expr, tp, args, prog);
                    s->as.match_stmt.branches[i].pattern = p;
                }
            }
            break;
        }

        case FREE_S:
            s->as.free_stmt.varName = x_strdup(src->as.free_stmt.varName);
            break;

        case EXPR_STMT_S:
            s->as.expr_stmt = clone_expr(src->as.expr_stmt, tp, args, prog);
            break;
    }
    return s;
}

// ----------------------------------------------------------------------------
// Top-level cloners: produce a concrete monomorphised decl from a template.
// ----------------------------------------------------------------------------

static FuncSign* clone_sign(const FuncSign* src, const TypeParamList* tp,
                            const TypeArgList* args, const char* mangled_name) {
    FuncSign* s = malloc(sizeof(FuncSign));
    *s = *src;
    s->name        = x_strdup(mangled_name);
    s->retTypeName = src->retTypeName ? x_strdup(src->retTypeName) : NULL;
    s->parameters  = malloc(sizeof(FuncParam) * src->paramNum);
    for (int i = 0; i < src->paramNum; ++i) {
        s->parameters[i] = src->parameters[i];
        s->parameters[i].name      = x_strdup(src->parameters[i].name);
        s->parameters[i].type_name = src->parameters[i].type_name ? x_strdup(src->parameters[i].type_name) : NULL;
    }
    subst_func_sign(s, tp, args);
    return s;
}

static Func* instantiate_func(const Func* tpl, const TypeArgList* args,
                              const char* mangled, Program* prog) {
    Func* f = malloc(sizeof(Func));
    // Make `prog` visible to subst_type so it can queue any nested
    // struct instantiations the substitution discovers (e.g. when a
    // body parameter type was eagerly mangled to `List__T` at parse
    // time and now becomes `List__int`).
    Program* prev_prog = g_subst_prog;
    SourceLocation prev_loc = g_subst_loc;
    g_subst_prog = prog;
    g_subst_loc  = tpl->signature ? (SourceLocation){0, 0, NULL} : (SourceLocation){0, 0, NULL};
    f->signature   = clone_sign(tpl->signature, tpl->type_params, args, mangled);
    f->body        = clone_stmt(tpl->body, tpl->type_params, args, prog);
    g_subst_prog = prev_prog;
    g_subst_loc  = prev_loc;
    f->attrs       = NULL;       // attrs don't carry across instantiations for v1
    f->type_params = NULL;       // concrete now
    // Carry OOP affiliation across the monomorphisation so the analyzer's
    // private/static dispatch sees the same metadata on the concrete fn
    // that it would on the template.
    f->is_private  = tpl->is_private;
    f->is_static   = tpl->is_static;
    f->owner_struct = tpl->owner_struct ? x_strdup(tpl->owner_struct) : NULL;
    return f;
}

static StructDecl* instantiate_struct(const StructDecl* tpl, const TypeArgList* args,
                                       const char* mangled, Program* prog) {
    StructDecl* d = malloc(sizeof(StructDecl));
    d->name        = x_strdup(mangled);
    d->loc         = tpl->loc;
    d->attrs       = NULL;
    d->type_params = NULL;
    d->field_count = tpl->field_count;
    d->fields      = malloc(sizeof(StructField) * tpl->field_count);

    // Same prog-threading trick as instantiate_func: subst_type can
    // discover nested template uses (a struct field of type Map<T> etc.)
    // and needs to queue them for the drain.
    Program* prev_prog = g_subst_prog;
    SourceLocation prev_loc = g_subst_loc;
    g_subst_prog = prog;
    g_subst_loc  = tpl->loc;
    for (int i = 0; i < tpl->field_count; ++i) {
        d->fields[i] = tpl->fields[i];
        d->fields[i].name      = x_strdup(tpl->fields[i].name);
        d->fields[i].type_name = tpl->fields[i].type_name ? x_strdup(tpl->fields[i].type_name) : NULL;
        subst_type(&d->fields[i].type, &d->fields[i].type_name, tpl->type_params, args);
    }
    g_subst_prog = prev_prog;
    g_subst_loc  = prev_loc;
    return d;
}

// ----------------------------------------------------------------------------
// Drain pass.
// ----------------------------------------------------------------------------

static const Func* find_func_template(const Program* p, const char* name) {
    for (int i = 0; i < p->func_count; ++i) {
        Func* f = p->functions[i];
        if (f && f->type_params && f->signature &&
            strcmp(f->signature->name, name) == 0) return f;
    }
    return NULL;
}

// True when `mangled` is already declared as a (non-template) function or
// extern in the program. Used to short-circuit the template-resolution
// path: when a host plugin emits `def Add__Health(...)` directly as an
// extern, callers writing `Add<Health>(...)` should bind to that extern,
// not error out asking for a missing template named `Add`.
static bool is_concrete_func_declared(const Program* p, const char* mangled) {
    for (int i = 0; i < p->func_count; ++i) {
        Func* f = p->functions[i];
        if (!f || !f->signature) continue;
        if (f->type_params) continue;   // skip templates
        if (strcmp(f->signature->name, mangled) == 0) return true;
    }
    for (int i = 0; i < p->ext_block_count; ++i) {
        ExternBlock* eb = p->externBlocks[i];
        if (!eb) continue;
        for (int j = 0; j < eb->count; ++j) {
            FuncSign* fs = eb->signs[j];
            if (fs && fs->name && strcmp(fs->name, mangled) == 0) return true;
        }
    }
    return false;
}

static const StructDecl* find_struct_template(const Program* p, const char* name) {
    for (int i = 0; i < p->struct_count; ++i) {
        StructDecl* s = p->structs[i];
        if (s && s->type_params && strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

static void prog_add_func(Program* p, Func* f) {
    p->functions = realloc(p->functions, sizeof(Func*) * (p->func_count + 1));
    p->functions[p->func_count++] = f;
}

static void prog_add_struct(Program* p, StructDecl* s) {
    p->structs = realloc(p->structs, sizeof(StructDecl*) * (p->struct_count + 1));
    p->structs[p->struct_count++] = s;
}

void tpl_drain_pending(Program* p, bool strict) {
    if (!p) return;
    // Carry list for pendings whose template isn't visible yet. In non-strict
    // mode we set them aside instead of erroring — the caller will drain
    // again later (e.g. after stdlib merge) and a final strict pass reports
    // anything still unresolved.
    PendingInstantiation** kept = NULL;
    int kept_count = 0;
    int kept_cap = 0;
#define KEEP_PENDING(pi) do { \
    if (kept_count >= kept_cap) { \
        kept_cap = kept_cap ? kept_cap * 2 : 4; \
        kept = realloc(kept, sizeof(PendingInstantiation*) * kept_cap); \
    } \
    kept[kept_count++] = (pi); \
} while (0)

    // Drain in a loop — instantiating one template may queue more (a template
    // body that itself uses Foo<T>).
    while (p->pending_count > 0) {
        // Pop one off the front (FIFO).
        PendingInstantiation* pi = p->pending[0];
        for (int i = 1; i < p->pending_count; ++i) p->pending[i - 1] = p->pending[i];
        p->pending_count--;

        if (tpl_already_instantiated(p, pi->mangled_name)) {
            // already done — drop
            free(pi->mangled_name); free(pi->template_name); free(pi);
            continue;
        }

        if (pi->kind == 0) {
            // If a concrete function/extern with the mangled name already
            // exists (e.g. plugin-emitted `Add__Health`), the call site
            // resolves directly to it — no template instantiation needed.
            if (is_concrete_func_declared(p, pi->mangled_name)) {
                tpl_remember(p, pi->mangled_name);
                free(pi->mangled_name); free(pi->template_name); free(pi);
                continue;
            }
            const Func* tpl = find_func_template(p, pi->template_name);
            if (!tpl) {
                if (!strict) { KEEP_PENDING(pi); continue; }
                stage_error(STAGE_PARSER, pi->use_loc,
                    "no function template '%s' found for use of '%s'",
                    pi->template_name, pi->mangled_name);
                free(pi->mangled_name); free(pi->template_name); free(pi);
                continue;
            }
            if (tpl->type_params->count != pi->type_args->count) {
                stage_error(STAGE_PARSER, pi->use_loc,
                    "template '%s' expects %d type argument(s), got %d",
                    pi->template_name, tpl->type_params->count, pi->type_args->count);
                free(pi->mangled_name); free(pi->template_name); free(pi);
                continue;
            }
            Func* concrete = instantiate_func(tpl, pi->type_args, pi->mangled_name, p);
            prog_add_func(p, concrete);
            tpl_remember(p, pi->mangled_name);
        } else {
            const StructDecl* tpl = find_struct_template(p, pi->template_name);
            if (!tpl) {
                if (!strict) { KEEP_PENDING(pi); continue; }
                stage_error(STAGE_PARSER, pi->use_loc,
                    "no struct template '%s' found for use of '%s'",
                    pi->template_name, pi->mangled_name);
                free(pi->mangled_name); free(pi->template_name); free(pi);
                continue;
            }
            if (tpl->type_params->count != pi->type_args->count) {
                stage_error(STAGE_PARSER, pi->use_loc,
                    "template '%s' expects %d type argument(s), got %d",
                    pi->template_name, tpl->type_params->count, pi->type_args->count);
                free(pi->mangled_name); free(pi->template_name); free(pi);
                continue;
            }
            StructDecl* concrete = instantiate_struct(tpl, pi->type_args, pi->mangled_name, p);
            prog_add_struct(p, concrete);
            tpl_remember(p, pi->mangled_name);

            // Eager method instantiation: every function template named
            // `<StructBase><Method>` with matching type-param arity gets
            // instantiated against the same args. This is what makes
            // `xs.Push(7)` resolve to ListPush__int automatically.
            //
            // Skip if any of pi's type-args are themselves a template
            // parameter name (e.g. `List__T` was pushed as an internal
            // placeholder by the parser when it saw `ref List<T>`
            // inside a generic def -- those are NOT real
            // instantiations, just textual mangle placeholders, and
            // forwarding them would emit broken `ListPush__T` C code).
            bool args_concrete = true;
            for (int a = 0; pi->type_args && a < pi->type_args->count; ++a) {
                const TypeArg* ta = &pi->type_args->args[a];
                if (ta->type == VAR_T && ta->type_name) {
                    // Only accept VAR_T args that name a real struct
                    // already instantiated. Bare template params (T, U)
                    // never appear as struct names so they fail this.
                    bool ok = false;
                    for (int s = 0; s < p->struct_count; ++s) {
                        if (p->structs[s] && p->structs[s]->name &&
                            strcmp(p->structs[s]->name, ta->type_name) == 0) {
                            ok = true; break;
                        }
                    }
                    if (!ok) { args_concrete = false; break; }
                }
            }
            if (args_concrete && tpl->type_params && pi->type_args) {
                int base_len = (int)strlen(pi->template_name);
                for (int fi = 0; fi < p->func_count; ++fi) {
                    const Func* ftpl = p->functions[fi];
                    if (!ftpl || !ftpl->signature || !ftpl->type_params) continue;
                    if (ftpl->type_params->count != pi->type_args->count) continue;
                    const char* fn = ftpl->signature->name;
                    if (!fn) continue;
                    if (strncmp(fn, pi->template_name, base_len) != 0) continue;
                    // Suffix must be a valid PascalCase Method or
                    // _underscore-prefixed internal helper. Stops
                    // "ListNew" matching "ListNewer" too aggressively.
                    char first = fn[base_len];
                    if (first == 0) continue;          // exact match -- not a method
                    if (!(first >= 'A' && first <= 'Z') && first != '_') continue;
                    char* method_mangled = tpl_mangle(fn, pi->type_args);
                    if (tpl_already_instantiated(p, method_mangled)) {
                        free(method_mangled);
                        continue;
                    }
                    // Clone the type-args list per instantiation so each
                    // pending entry owns its own. The caller's
                    // `pi->type_args` is freed below; sharing here would
                    // double-free.
                    TypeArgList* tal = malloc(sizeof(TypeArgList));
                    tal->count = pi->type_args->count;
                    tal->args  = malloc(sizeof(TypeArg) * tal->count);
                    for (int a = 0; a < tal->count; ++a) {
                        tal->args[a] = pi->type_args->args[a];
                        if (tal->args[a].type == VAR_T && tal->args[a].type_name)
                            tal->args[a].type_name = x_strdup(tal->args[a].type_name);
                    }
                    tpl_push_pending(p, x_strdup(fn), tal,
                                      method_mangled, pi->use_loc, /*kind=func*/ 0);
                }
            }
        }
        free(pi->mangled_name); free(pi->template_name); free(pi);
    }

    // Restore unresolved pendings so a later (post-merge) drain can retry.
    if (kept_count > 0) {
        if (p->pending_capacity < kept_count) {
            p->pending_capacity = kept_count;
            p->pending = realloc(p->pending,
                sizeof(PendingInstantiation*) * p->pending_capacity);
        }
        for (int i = 0; i < kept_count; ++i) p->pending[i] = kept[i];
        p->pending_count = kept_count;
    }
    free(kept);
#undef KEEP_PENDING
}
