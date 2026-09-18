#ifndef IATHOOK_H
#define IATHOOK_H

#include <stddef.h>
#include <windows.h>

/* Patch one named import in module's IAT. Returns previous pointer, or NULL. */
void* IatHook(HMODULE module, const char* importDll, const char* funcName, void* newFunc);

typedef struct IatHookEntry {
    const char* dll;
    const char* name;
    void* hook;
    void** real; /* optional; receives previous IAT pointer */
} IatHookEntry;

typedef void (*IatHookMissingFn)(const char* name, void* ctx);

/* Apply each entry. Returns how many hooks succeeded. */
int IatHookAll(HMODULE module, const IatHookEntry* entries, size_t count, IatHookMissingFn on_missing,
               void* ctx);

#endif
