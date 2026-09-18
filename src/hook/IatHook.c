#include "IatHook.h"

#include <string.h>

static int DllNameMatch(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

void* IatHook(HMODULE module, const char* importDll, const char* funcName, void* newFunc) {
    if (!module || !importDll || !funcName || !newFunc) return NULL;

    BYTE* base = (BYTE*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return NULL;

    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* dll = (const char*)(base + imp->Name);
        if (!DllNameMatch(dll, importDll)) continue;

        IMAGE_THUNK_DATA* oft = imp->OriginalFirstThunk
                                    ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk)
                                    : (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA* iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);

        for (; oft->u1.AddressOfData; ++oft, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;

            IMAGE_IMPORT_BY_NAME* byName = (IMAGE_IMPORT_BY_NAME*)(base + oft->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, funcName) != 0) continue;

            void* old = (void*)iat->u1.Function;
            DWORD protect = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &protect)) {
                return NULL;
            }
            iat->u1.Function = (ULONG_PTR)newFunc;
            VirtualProtect(&iat->u1.Function, sizeof(void*), protect, &protect);
            return old;
        }
    }
    return NULL;
}

int IatHookAll(HMODULE module, const IatHookEntry* entries, size_t count, IatHookMissingFn on_missing,
               void* ctx) {
    if (!module || !entries || !count) return 0;

    int n = 0;
    for (size_t i = 0; i < count; ++i) {
        void* prev = IatHook(module, entries[i].dll, entries[i].name, entries[i].hook);
        if (!prev) {
            if (on_missing) on_missing(entries[i].name, ctx);
            continue;
        }
        if (entries[i].real) *entries[i].real = prev;
        ++n;
    }
    return n;
}
