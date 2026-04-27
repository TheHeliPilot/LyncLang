// Echo plugin — minimal Lync plugin that prints what it sees.
//
// For every top-level decl with attributes, it logs the name + each
// attribute (with arg values) to stderr, then emits a comment + a no-op
// init line for each [echo] attribute it finds, so you can see the
// dispatch + emit_top + emit_init paths all working in the .c output.
//
// Build:
//   clang -shared -o echo_plugin.dll echo_plugin.c -I../src    (Windows)
//   cc -shared -fPIC -o echo_plugin.so echo_plugin.c -I../src  (POSIX)
//
// Run:
//   lync mygame.lync --plugin=echo_plugin.dll --emit-c

#include "../../src/lync_plugin.h"

#include <stdio.h>
#include <string.h>

static void on_load(LyncContext* ctx, const LyncApi* api) {
    (void)ctx; (void)api;
    fprintf(stderr, "[echo] loaded\n");
}

static void on_decl(LyncContext* ctx, const LyncApi* api, LyncDecl* d) {
    const int attr_count = api->decl_attr_count(d);
    if (attr_count == 0) return;   // no attrs → not interesting

    const char* name = api->decl_name(d);
    const char* kind_str = (api->decl_kind(d) == LYNC_DECL_STRUCT) ? "struct" : "func";
    fprintf(stderr, "[echo] %s '%s' has %d attribute(s):\n",
            kind_str, name, attr_count);

    for (int i = 0; i < attr_count; ++i) {
        LyncAttr* a = api->decl_attr_at(d, i);
        const char* aname = api->attr_name(a);
        const int   args  = api->attr_arg_count(a);
        fprintf(stderr, "[echo]   - %s(", aname);
        for (int j = 0; j < args; ++j) {
            if (j > 0) fprintf(stderr, ", ");
            switch (api->attr_arg_kind(a, j)) {
                case LYNC_ATTR_INT:
                    fprintf(stderr, "%d", api->attr_arg_int(a, j));
                    break;
                case LYNC_ATTR_BOOL:
                    fprintf(stderr, "%s", api->attr_arg_bool(a, j) ? "true" : "false");
                    break;
                case LYNC_ATTR_STRING:
                    fprintf(stderr, "\"%s\"", api->attr_arg_string(a, j));
                    break;
            }
        }
        fprintf(stderr, ")\n");

        // For [echo] specifically: prove emit_top + emit_init both reach
        // the output. Other attributes are just listed.
        if (strcmp(aname, "echo") == 0) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "/* echo plugin saw: %s '%s' */\n",
                     kind_str, name);
            api->emit_top(ctx, buf);

            snprintf(buf, sizeof(buf),
                     "    /* echo init for %s */ (void)0;\n", name);
            api->emit_init(ctx, buf);
        }

        // For [component] on structs: emit a sample reflection table the
        // way the eventual Zues plugin will. Proves struct introspection.
        if (strcmp(aname, "component") == 0 &&
            api->decl_kind(d) == LYNC_DECL_STRUCT) {
            char buf[1024];
            int  n = snprintf(buf, sizeof(buf),
                "/* component reflection for %s */\n"
                "typedef struct { const char* name; const char* type; } %s_FieldInfo;\n"
                "static const %s_FieldInfo %s_fields[] = {\n",
                name, name, name, name);
            api->emit_top(ctx, buf);

            const int fc = api->struct_field_count(d);
            for (int k = 0; k < fc; ++k) {
                snprintf(buf, sizeof(buf),
                    "    { \"%s\", \"%s\" },\n",
                    api->struct_field_name(d, k),
                    api->struct_field_type(d, k));
                api->emit_top(ctx, buf);
            }
            snprintf(buf, sizeof(buf), "};\n\n");
            api->emit_top(ctx, buf);
        }
    }
}

static void on_finalize(LyncContext* ctx, const LyncApi* api) {
    (void)ctx; (void)api;
    fprintf(stderr, "[echo] finalized\n");
}

static void on_unload(LyncContext* ctx) {
    (void)ctx;
    fprintf(stderr, "[echo] unloaded\n");
}

static const LyncPlugin g_plugin = {
    .abi_version = LYNC_PLUGIN_ABI_VERSION,
    .name        = "echo",
    .version     = "0.1",
    .on_load     = on_load,
    .on_decl     = on_decl,
    .on_finalize = on_finalize,
    .on_unload   = on_unload,
};

LYNC_PLUGIN_EXPORT const LyncPlugin* lync_plugin_entry(void) {
    return &g_plugin;
}
