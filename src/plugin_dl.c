// Platform-specific DLL handle wrappers, isolated in its own translation
// unit so <windows.h> never shares a scope with parser.h. (Windows defines
// an enumerator named `TokenType` in winnt.h that collides with our typedef.)
//
// Exported as plain extern "C" functions taking void* handles. plugin.c
// calls these without ever including windows.h.

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <stdio.h>

void* lync_dl_open(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* lync_dl_sym(void* handle, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}

void lync_dl_close(void* handle) {
#if defined(_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

const char* lync_dl_err(void) {
#if defined(_WIN32)
    static char buf[64];
    snprintf(buf, sizeof(buf), "GetLastError=%lu", (unsigned long)GetLastError());
    return buf;
#else
    const char* m = dlerror();
    return m ? m : "unknown";
#endif
}
