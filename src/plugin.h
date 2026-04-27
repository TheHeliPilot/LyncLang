// Compiler-side plugin loader. Public surface used by main.c.
//
// Lifecycle from main:
//   1. plugin_load(path) for each --plugin=path argument
//   2. plugin_dispatch_load(ctx)
//   3. After analyzer succeeds, plugin_dispatch_decls(ctx, program)
//   4. generate_code() runs (existing codegen)
//   5. plugin_dispatch_finalize(ctx) — flushes plugin emissions to file
//   6. plugin_dispatch_unload(ctx)
//   7. plugin_shutdown() releases handles
//
// LyncContext owns the output FILE* + the deferred emission buffers.

#ifndef LYNC_LOADER_H
#define LYNC_LOADER_H

#include "common.h"
#include "parser.h"
#include "lync_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

// Allocate + init. Caller passes the output FILE the codegen is writing to.
// May be NULL if the file isn't open yet (set later via _set_output).
LyncContext* plugin_context_create(FILE* out);
void         plugin_context_destroy(LyncContext* ctx);
void         plugin_context_set_output(LyncContext* ctx, FILE* out);

// Take ownership of the accumulated lync_decls buffer (filled by plugins'
// on_decl_pre_analyze hooks). Caller must free(). NULL if empty. Resets
// the buffer on the context.
char*        plugin_context_take_lync_decls(LyncContext* ctx);

// Load a plugin DLL by absolute or relative path. Returns true on success.
// Failures (file missing, bad ABI version, missing entry point) are logged
// to stderr and the load is skipped.
bool plugin_load(const char* path);

// How many plugins were successfully loaded so far.
int plugin_count(void);

// Hook dispatchers. Each calls every loaded plugin's matching hook in
// load order. Safe to call when no plugins are loaded (no-op).
void plugin_dispatch_load             (LyncContext* ctx);
void plugin_dispatch_decls_pre_analyze(LyncContext* ctx, Program* prog);
void plugin_dispatch_decls            (LyncContext* ctx, Program* prog);
void plugin_dispatch_finalize(LyncContext* ctx);
void plugin_dispatch_unload  (LyncContext* ctx);

// Free all plugin handles. Call once at compiler exit.
void plugin_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
