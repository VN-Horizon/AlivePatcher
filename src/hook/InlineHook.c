#include "InlineHook.h"

#include <string.h>

void* InlineHook(void* src, void* hook, size_t stolen, const BYTE* expect) {
    if (!src || !hook || stolen < 5 || !expect) return NULL;

    BYTE* p = (BYTE*)src;
    if (memcmp(p, expect, stolen) != 0) return NULL;

    BYTE* tramp = (BYTE*)VirtualAlloc(NULL, stolen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;

    memcpy(tramp, p, stolen);
    tramp[stolen] = 0xE9;
    {
        const INT32 back = (INT32)((p + stolen) - (tramp + stolen + 5));
        memcpy(tramp + stolen + 1, &back, 4);
    }

    DWORD old = 0;
    if (!VirtualProtect(p, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return NULL;
    }
    p[0] = 0xE9;
    {
        const INT32 rel = (INT32)((BYTE*)hook - (p + 5));
        memcpy(p + 1, &rel, 4);
    }
    if (stolen > 5) memset(p + 5, 0x90, stolen - 5);
    VirtualProtect(p, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, stolen);
    FlushInstructionCache(GetCurrentProcess(), tramp, stolen + 5);
    return tramp;
}
