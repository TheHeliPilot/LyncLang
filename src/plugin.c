#include "plugin.h"
#include "codegen.h"

#include <stdlib.h>
#include <string.h>

// Implemented in plugin_dl.c — kept in a separate TU so windows.h never
// shares a scope with parser.h (Windows' winnt.h defines a `TokenType`
// enumerator that collides with ours).
extern void*       lync_dl_open (const char* path);
extern void*       lync_dl_sym  (void* handle, const char* name);
extern void        lync_dl_close(void* handle);
extern const char* lync_dl_err  (void);

typedef void* PluginHandle;
static PluginHandle h_open (const char* path)            { return lync_dl_open(path); }
static void*        h_sym  (PluginHandle h, const char* n) { return lync_dl_sym(h, n); }
static void         h_close(PluginHandle h)              { lync_dl_close(h); }
static const char*  h_err  (void)                        { return lync_dl_err(); }

// =============================================================================
// LyncContext + decl/attr opaque wrappers
// =============================================================================

// LyncDecl is a small tagged union over the two internal AST node types.
// Allocated transiently per dispatch call (we don't keep them between
// frames). Kept as an array indexed by the program's decl ordering so
// plugins receive stable handles within a single compile.
struct LyncDecl {
    int kind;                        // LYNC_DECL_STRUCT or LYNC_DECL_FUNC
    union {
        StructDecl* sd;
        Func*       fn;
    } as;
};

// LyncAttr passes the internal Attribute pointer through. Plugins only see
// it as opaque; the cast happens in the api accessors below.
//
// (No wrapper needed — Attribute* is stable inside one compile.)

// LyncContext holds the output file + two deferred emission buffers.
// emit_top   → appended to .c after user functions
// emit_init  → wrapped in a single auto-generated __lync_plugin_init()
struct LyncContext {
    FILE* out;

    char*  top_buf;
    size_t top_len;
    size_t top_cap;

    char*  init_buf;
    size_t init_len;
    size_t init_cap;

    // v2 Lync-source injection buffer. Filled via api_emit_lync_decls
    // during on_decl_pre_analyze; main.c reads it back, parses it, and
    // merges its extern blocks into the user program before analysis.
    char*  lync_buf;
    size_t lync_len;
    size_t lync_cap;
};

// Append to a growable byte buffer. Cheap; doubles when exhausted.
static void buf_append(char** buf, size_t* len, size_t* cap, const char* s) {
    if (!s || !*s) return;
    const size_t add = strlen(s);
    if (*len + add + 1 > *cap) {
        size_t nc = *cap ? *cap : 256;
        while (nc < *len + add + 1) nc *= 2;
        *buf = realloc(*buf, nc);
        *cap = nc;
    }
    memcpy(*buf + *len, s, add);
    *len += add;
    (*buf)[*len] = '\0';
}

LyncContext* plugin_context_create(FILE* out) {
    LyncContext* c = calloc(1, sizeof(LyncContext));
    c->out = out;
    return c;
}
void plugin_context_destroy(LyncContext* ctx) {
    if (!ctx) return;
    free(ctx->top_buf);
    free(ctx->init_buf);
    free(ctx->lync_buf);
    free(ctx);
}

void plugin_context_set_output(LyncContext* ctx, FILE* out) {
    if (ctx) ctx->out = out;
}

// Hand the accumulated lync_buf back to the caller. Caller takes ownership
// and must free(). Buffer is reset on the context. Returns NULL if empty.
char* plugin_context_take_lync_decls(LyncContext* ctx) {
    if (!ctx || ctx->lync_len == 0) return NULL;
    char* out = ctx->lync_buf;
    ctx->lync_buf = NULL;
    ctx->lync_len = 0;
    ctx->lync_cap = 0;
    return out;
}

// =============================================================================
// LyncApi vtable implementations
// =============================================================================

static int api_decl_kind(LyncDecl* d) { return d->kind; }

static const char* api_decl_name(LyncDecl* d) {
    return d->kind == LYNC_DECL_STRUCT
        ? d->as.sd->name
        : d->as.fn->signature->name;
}

static AttributeList* decl_attrs(LyncDecl* d) {
    return d->kind == LYNC_DECL_STRUCT ? d->as.sd->attrs : d->as.fn->attrs;
}

static int api_decl_attr_count(LyncDecl* d) {
    AttributeList* al = decl_attrs(d);
    return al ? al->count : 0;
}

static LyncAttr* api_decl_attr_at(LyncDecl* d, int idx) {
    AttributeList* al = decl_attrs(d);
    if (!al || idx < 0 || idx >= al->count) return NULL;
    return (LyncAttr*)al->items[idx];
}

static const char* api_attr_name(LyncAttr* a) {
    return a ? ((Attribute*)a)->name : NULL;
}

static int api_attr_arg_count(LyncAttr* a) {
    return a ? ((Attribute*)a)->arg_count : 0;
}

static int api_attr_arg_kind(LyncAttr* a, int idx) {
    Attribute* atr = (Attribute*)a;
    if (!atr || idx < 0 || idx >= atr->arg_count) return -1;
    switch (atr->args[idx].kind) {
        case ATTR_ARG_INT:    return LYNC_ATTR_INT;
        case ATTR_ARG_BOOL:   return LYNC_ATTR_BOOL;
        case ATTR_ARG_STRING: return LYNC_ATTR_STRING;
    }
    return -1;
}

static int api_attr_arg_int(LyncAttr* a, int idx) {
    Attribute* atr = (Attribute*)a;
    if (!atr || idx < 0 || idx >= atr->arg_count) return 0;
    return atr->args[idx].int_val;
}
static int api_attr_arg_bool(LyncAttr* a, int idx) {
    return api_attr_arg_int(a, idx);   // same storage; kind disambiguates
}
static const char* api_attr_arg_string(LyncAttr* a, int idx) {
    Attribute* atr = (Attribute*)a;
    if (!atr || idx < 0 || idx >= atr->arg_count) return NULL;
    return atr->args[idx].str_val;
}

// ---- Struct field introspection ----
static int api_struct_field_count(LyncDecl* d) {
    if (d->kind != LYNC_DECL_STRUCT) return 0;
    return d->as.sd->field_count;
}

static const char* api_struct_field_name(LyncDecl* d, int idx) {
    if (d->kind != LYNC_DECL_STRUCT) return NULL;
    if (idx < 0 || idx >= d->as.sd->field_count) return NULL;
    return d->as.sd->fields[idx].name;
}

static const char* api_struct_field_type(LyncDecl* d, int idx) {
    if (d->kind != LYNC_DECL_STRUCT) return NULL;
    if (idx < 0 || idx >= d->as.sd->field_count) return NULL;
    StructField* f = &d->as.sd->fields[idx];
    if (f->type == VAR_T && f->type_name) return f->type_name;
    // Hand back the C-equivalent name. Mirrors codegen::type_to_c_type but
    // we don't depend on it here to keep this file self-contained.
    switch (f->type) {
        case INT_KEYWORD_T:    return "int";
        case BOOL_KEYWORD_T:   return "bool";
        case CHAR_KEYWORD_T:   return "char";
        case STR_KEYWORD_T:    return "char*";
        case FLOAT_KEYWORD_T:  return "float";
        case DOUBLE_KEYWORD_T: return "double";
        case VOID_KEYWORD_T:   return "void";
        case PTR_KEYWORD_T:    return "void*";
        default:               return "/*unknown*/ int";
    }
}

// ---- Function introspection ----
static const char* c_type_for_field(TokenType t, const char* type_name) {
    if (t == VAR_T && type_name) return type_name;
    switch (t) {
        case INT_KEYWORD_T:    return "int";
        case BOOL_KEYWORD_T:   return "bool";
        case CHAR_KEYWORD_T:   return "char";
        case STR_KEYWORD_T:    return "char*";
        case FLOAT_KEYWORD_T:  return "float";
        case DOUBLE_KEYWORD_T: return "double";
        case VOID_KEYWORD_T:   return "void";
        case PTR_KEYWORD_T:    return "void*";
        default:               return "int";
    }
}

static void api_func_mangled_name(LyncDecl* d, char* out_buf, int buf_size) {
    if (!out_buf || buf_size <= 0) return;
    out_buf[0] = '\0';
    if (!d || d->kind != LYNC_DECL_FUNC) return;
    char* m = get_mangled_name(d->as.fn->signature);
    if (m) {
        // Copy into caller buffer — get_mangled_name's static buffer can be
        // overwritten by other codegen activity between accessor calls.
        size_t n = strlen(m);
        if ((int)n >= buf_size) n = (size_t)buf_size - 1;
        memcpy(out_buf, m, n);
        out_buf[n] = '\0';
    }
}

static int api_func_param_count(LyncDecl* d) {
    if (!d || d->kind != LYNC_DECL_FUNC) return 0;
    return d->as.fn->signature->paramNum;
}

static const char* api_func_param_name(LyncDecl* d, int idx) {
    if (!d || d->kind != LYNC_DECL_FUNC) return NULL;
    if (idx < 0 || idx >= d->as.fn->signature->paramNum) return NULL;
    return d->as.fn->signature->parameters[idx].name;
}

static const char* api_func_param_type(LyncDecl* d, int idx) {
    if (!d || d->kind != LYNC_DECL_FUNC) return NULL;
    if (idx < 0 || idx >= d->as.fn->signature->paramNum) return NULL;
    FuncParam* p = &d->as.fn->signature->parameters[idx];
    return c_type_for_field(p->type, p->type_name);
}

static const char* api_func_return_type(LyncDecl* d) {
    if (!d || d->kind != LYNC_DECL_FUNC) return NULL;
    return c_type_for_field(d->as.fn->signature->retType,
                             d->as.fn->signature->retTypeName);
}

// ---- Codegen helpers ----
static FILE* api_output_file(LyncContext* ctx) { return ctx ? ctx->out : NULL; }

static void api_emit_top(LyncContext* ctx, const char* code) {
    if (!ctx) return;
    buf_append(&ctx->top_buf, &ctx->top_len, &ctx->top_cap, code);
}
static void api_emit_init(LyncContext* ctx, const char* code) {
    if (!ctx) return;
    buf_append(&ctx->init_buf, &ctx->init_len, &ctx->init_cap, code);
}
static void api_emit_lync_decls(LyncContext* ctx, const char* lync_source) {
    if (!ctx) return;
    buf_append(&ctx->lync_buf, &ctx->lync_len, &ctx->lync_cap, lync_source);
}

static const LyncApi g_api = {
    .abi_version          = LYNC_PLUGIN_ABI_VERSION,
    .decl_kind            = api_decl_kind,
    .decl_name            = api_decl_name,
    .decl_attr_count      = api_decl_attr_count,
    .decl_attr_at         = api_decl_attr_at,
    .attr_name            = api_attr_name,
    .attr_arg_count       = api_attr_arg_count,
    .attr_arg_kind        = api_attr_arg_kind,
    .attr_arg_int         = api_attr_arg_int,
    .attr_arg_bool        = api_attr_arg_bool,
    .attr_arg_string      = api_attr_arg_string,
    .struct_field_count   = api_struct_field_count,
    .struct_field_name    = api_struct_field_name,
    .struct_field_type    = api_struct_field_type,
    .func_mangled_name    = api_func_mangled_name,
    .func_param_count     = api_func_param_count,
    .func_param_name      = api_func_param_name,
    .func_param_type      = api_func_param_type,
    .func_return_type     = api_func_return_type,
    .emit_top             = api_emit_top,
    .emit_init            = api_emit_init,
    .output_file          = api_output_file,
    .emit_lync_decls      = api_emit_lync_decls,
};

// =============================================================================
// Plugin registry
// =============================================================================

#define MAX_PLUGINS 16

typedef struct {
    PluginHandle      handle;
    const LyncPlugin* plugin;
    char*             path;       // for error messages
} LoadedPlugin;

static LoadedPlugin g_plugins[MAX_PLUGINS];
static int          g_plugin_count = 0;

bool plugin_load(const char* path) {
    if (g_plugin_count >= MAX_PLUGINS) {
        fprintf(stderr, "[plugin] too many plugins (max %d)\n", MAX_PLUGINS);
        return false;
    }
    PluginHandle h = h_open(path);
    if (!h) {
        fprintf(stderr, "[plugin] cannot load '%s': %s\n", path, h_err());
        return false;
    }
    typedef const LyncPlugin* (*EntryFn)(void);
    EntryFn entry = (EntryFn)h_sym(h, "lync_plugin_entry");
    if (!entry) {
        fprintf(stderr, "[plugin] '%s' missing 'lync_plugin_entry'\n", path);
        h_close(h);
        return false;
    }
    const LyncPlugin* p = entry();
    if (!p) {
        fprintf(stderr, "[plugin] '%s' lync_plugin_entry returned NULL\n", path);
        h_close(h);
        return false;
    }
    if (p->abi_version != LYNC_PLUGIN_ABI_VERSION) {
        fprintf(stderr,
                "[plugin] '%s' ABI mismatch: plugin=%u compiler=%u (rebuild plugin)\n",
                path, p->abi_version, LYNC_PLUGIN_ABI_VERSION);
        h_close(h);
        return false;
    }

    g_plugins[g_plugin_count++] = (LoadedPlugin){
        .handle = h, .plugin = p,
#if defined(_WIN32)
        .path = _strdup(path),
#else
        .path = strdup(path),
#endif
    };
    fprintf(stderr, "[plugin] loaded %s '%s' v%s\n",
            path, p->name ? p->name : "(unnamed)", p->version ? p->version : "?");
    return true;
}

int plugin_count(void) { return g_plugin_count; }

// ---- Hook dispatch -------------------------------------------------------

void plugin_dispatch_load(LyncContext* ctx) {
    for (int i = 0; i < g_plugin_count; ++i) {
        if (g_plugins[i].plugin->on_load)
            g_plugins[i].plugin->on_load(ctx, &g_api);
    }
}

void plugin_dispatch_decls_pre_analyze(LyncContext* ctx, Program* prog) {
    if (g_plugin_count == 0) return;
    for (int i = 0; i < prog->struct_count; ++i) {
        LyncDecl d = {.kind = LYNC_DECL_STRUCT, .as.sd = prog->structs[i]};
        for (int p = 0; p < g_plugin_count; ++p) {
            if (g_plugins[p].plugin->on_decl_pre_analyze)
                g_plugins[p].plugin->on_decl_pre_analyze(ctx, &g_api, &d);
        }
    }
    for (int i = 0; i < prog->func_count; ++i) {
        LyncDecl d = {.kind = LYNC_DECL_FUNC, .as.fn = prog->functions[i]};
        for (int p = 0; p < g_plugin_count; ++p) {
            if (g_plugins[p].plugin->on_decl_pre_analyze)
                g_plugins[p].plugin->on_decl_pre_analyze(ctx, &g_api, &d);
        }
    }
}

void plugin_dispatch_decls(LyncContext* ctx, Program* prog) {
    if (g_plugin_count == 0) return;

    // Visit structs first (they're declared first), then funcs. Order
    // matters for plugins that emit init code per decl — they get a
    // predictable sequence.
    for (int i = 0; i < prog->struct_count; ++i) {
        LyncDecl d = {.kind = LYNC_DECL_STRUCT, .as.sd = prog->structs[i]};
        for (int p = 0; p < g_plugin_count; ++p) {
            if (g_plugins[p].plugin->on_decl)
                g_plugins[p].plugin->on_decl(ctx, &g_api, &d);
        }
    }
    for (int i = 0; i < prog->func_count; ++i) {
        LyncDecl d = {.kind = LYNC_DECL_FUNC, .as.fn = prog->functions[i]};
        for (int p = 0; p < g_plugin_count; ++p) {
            if (g_plugins[p].plugin->on_decl)
                g_plugins[p].plugin->on_decl(ctx, &g_api, &d);
        }
    }
}

void plugin_dispatch_finalize(LyncContext* ctx) {
    if (!ctx) return;

    for (int i = 0; i < g_plugin_count; ++i) {
        if (g_plugins[i].plugin->on_finalize)
            g_plugins[i].plugin->on_finalize(ctx, &g_api);
    }

    // Flush deferred buffers to the output file. Top first (file-scope
    // declarations), then the init function wrapping any emit_init calls.
    if (ctx->top_len > 0) {
        fputs("\n/* ---- plugin top emissions ---- */\n", ctx->out);
        fwrite(ctx->top_buf, 1, ctx->top_len, ctx->out);
        if (ctx->top_buf[ctx->top_len - 1] != '\n') fputc('\n', ctx->out);
    }
    if (ctx->init_len > 0) {
        fputs("\n/* ---- plugin init function (auto-generated) ---- */\n", ctx->out);
        fputs("static void __lync_plugin_init(void) {\n", ctx->out);
        fwrite(ctx->init_buf, 1, ctx->init_len, ctx->out);
        if (ctx->init_buf[ctx->init_len - 1] != '\n') fputc('\n', ctx->out);
        fputs("}\n", ctx->out);
    }
}

void plugin_dispatch_unload(LyncContext* ctx) {
    for (int i = 0; i < g_plugin_count; ++i) {
        if (g_plugins[i].plugin->on_unload)
            g_plugins[i].plugin->on_unload(ctx);
    }
}

void plugin_shutdown(void) {
    for (int i = 0; i < g_plugin_count; ++i) {
        h_close(g_plugins[i].handle);
        free(g_plugins[i].path);
    }
    g_plugin_count = 0;
}
