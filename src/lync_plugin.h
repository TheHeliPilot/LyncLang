// Lync compiler plugin ABI.
//
// Plugins extend the compiler with semantics for `[attribute]` annotations.
// The Lync core stays general-purpose: parser collects attributes as opaque
// metadata, then walks every top-level decl through registered plugins so
// each can emit additional C code based on its own attribute set.
//
// Versioning: any change to LyncPlugin or LyncApi struct layout bumps
// LYNC_PLUGIN_ABI_VERSION. Plugins publish the version they were built
// against; mismatch = compiler refuses to load the plugin.
//
// Example minimum plugin:
//   const LyncPlugin* lync_plugin_entry(void) {
//       static const LyncPlugin p = {
//           .abi_version = LYNC_PLUGIN_ABI_VERSION,
//           .name        = "echo",
//           .version     = "0.1",
//           .on_decl     = my_on_decl,
//       };
//       return &p;
//   }

#ifndef LYNC_PLUGIN_H
#define LYNC_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

// v2: added on_decl_pre_analyze hook + emit_lync_decls API for plugins to
//     inject Lync source (extern blocks, helpers) before the analyzer runs.
//     Lets plugins kill hand-written boilerplate by synthesizing the decls
//     the user would otherwise write themselves.
#define LYNC_PLUGIN_ABI_VERSION 2u

// ---- Opaque handles ------------------------------------------------------
// Plugins receive these by value-pointer from the compiler. The internal
// shape is intentionally hidden — accessor functions in LyncApi are the
// only way to read fields. This lets the compiler grow its AST without
// breaking plugin binary compat.
typedef struct LyncContext LyncContext;
typedef struct LyncDecl    LyncDecl;
typedef struct LyncAttr    LyncAttr;

// ---- Decl kind ----------------------------------------------------------
typedef enum {
    LYNC_DECL_STRUCT = 0,
    LYNC_DECL_FUNC   = 1
} LyncDeclKind;

// ---- Attribute argument kind --------------------------------------------
typedef enum {
    LYNC_ATTR_INT    = 0,
    LYNC_ATTR_BOOL   = 1,
    LYNC_ATTR_STRING = 2
} LyncAttrArgKind;

// ---- Stable accessor table ----------------------------------------------
// Plugins receive a const pointer to one of these in every hook. The vtable
// is only ever appended to — adding fields bumps the version, removing or
// reordering is a hard break.
typedef struct LyncApi {
    uint32_t abi_version;

    // ---- Decl introspection ---------------------------------------------
    int          (*decl_kind)        (LyncDecl*);
    const char*  (*decl_name)        (LyncDecl*);
    int          (*decl_attr_count)  (LyncDecl*);
    LyncAttr*    (*decl_attr_at)     (LyncDecl*, int idx);

    // ---- Attribute introspection ----------------------------------------
    const char*  (*attr_name)        (LyncAttr*);
    int          (*attr_arg_count)   (LyncAttr*);
    int          (*attr_arg_kind)    (LyncAttr*, int idx);   // LyncAttrArgKind
    int          (*attr_arg_int)     (LyncAttr*, int idx);
    int          (*attr_arg_bool)    (LyncAttr*, int idx);
    const char*  (*attr_arg_string)  (LyncAttr*, int idx);

    // ---- Struct introspection (only valid when decl_kind == STRUCT) -----
    int          (*struct_field_count)(LyncDecl*);
    const char*  (*struct_field_name)(LyncDecl*, int idx);
    // Returns the C type name as the codegen would emit it ("int", "float",
    // "MyStructName", etc.). Plugins emitting reflection metadata can use
    // this directly in their generated code.
    const char*  (*struct_field_type)(LyncDecl*, int idx);

    // ---- Function introspection (only valid when decl_kind == FUNC) -----
    // Mangled C name as the codegen will emit it (e.g. `add_int_int_int`).
    // Plugins use this to reference user functions from generated code.
    // Result is copied into out_buf (caller-provided, recommend >=256).
    void         (*func_mangled_name)(LyncDecl*, char* out_buf, int buf_size);
    int          (*func_param_count) (LyncDecl*);
    const char*  (*func_param_name)  (LyncDecl*, int idx);
    const char*  (*func_param_type)  (LyncDecl*, int idx);   // C type name
    const char*  (*func_return_type) (LyncDecl*);            // C type name

    // ---- Codegen helpers ------------------------------------------------
    // emit_top: queues code to be appended at the END of the .c file
    //   (after the user's functions). For static data + plugin helpers.
    // emit_init: queues code into a single auto-generated init function
    //   __lync_plugin_init() that the user can call from their main if
    //   they want — or that plugins themselves emit a call to from on_finalize.
    void         (*emit_top) (LyncContext*, const char* c_code);
    void         (*emit_init)(LyncContext*, const char* c_code);

    // Direct write to the output file for cases where buffering is the
    // wrong move (rare). Use emit_top/emit_init when you can.
    FILE*        (*output_file)(LyncContext*);

    // ---- Lync source injection (v2) -------------------------------------
    // Append Lync source text to a per-context buffer. After all plugins
    // finish their on_decl_pre_analyze pass, the compiler parses this
    // buffer and merges its extern blocks / functions into the main
    // program — so the user can call those decls without writing them.
    // Typical use: emit `extern <stddef.h> { def helper(...): void; }`
    // for each [component] the plugin will auto-codegen a helper for.
    void         (*emit_lync_decls)(LyncContext*, const char* lync_source);
} LyncApi;

// ---- Plugin descriptor --------------------------------------------------
// Each plugin DLL exports `lync_plugin_entry()` returning a pointer to a
// static instance of this struct. All hook functions are optional — set
// to NULL the ones you don't need.
typedef struct LyncPlugin {
    uint32_t    abi_version;
    const char* name;
    const char* version;

    // Called once after the plugin is loaded, before any decls are visited.
    void (*on_load)    (LyncContext*, const LyncApi*);

    // Called for every top-level declaration (struct + func), in source
    // order. Plugins inspect attributes via the api and emit C code
    // (via api->emit_top / emit_init) to react to them.
    void (*on_decl)    (LyncContext*, const LyncApi*, LyncDecl*);

    // (v2) Called for every parsed top-level declaration BEFORE the
    // analyzer runs. Plugins can call api->emit_lync_decls(...) here to
    // synthesize Lync source — extern blocks, helper fns — that should
    // be visible to the user's code during analysis. No C-side emit yet;
    // use on_decl for that. NULL = no pre-analyze pass for this plugin.
    void (*on_decl_pre_analyze)(LyncContext*, const LyncApi*, LyncDecl*);

    // Called once after all decls have been visited. Final chance to emit.
    void (*on_finalize)(LyncContext*, const LyncApi*);

    // Called once after all hooks are done, before the compiler moves on.
    // Plugins free any state they allocated.
    void (*on_unload)  (LyncContext*);
} LyncPlugin;

// Visibility for the plugin's exported entry point.
#if defined(_WIN32)
    #define LYNC_PLUGIN_EXPORT __declspec(dllexport)
#else
    #define LYNC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// Each plugin DLL exports this symbol; the compiler dlsym's it.
LYNC_PLUGIN_EXPORT const LyncPlugin* lync_plugin_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* LYNC_PLUGIN_H */
