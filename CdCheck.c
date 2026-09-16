#include "CdCheck.h"

#include "IatHook.h"
#include "Logger.h"

#include <string.h>

enum { FIND_CD_AUDIO_RVA = 0x31180 }; /* VA 0x431180 */

static UINT(WINAPI* Real_GetPrivateProfileIntA)(LPCSTR, LPCSTR, INT, LPCSTR);

static UINT WINAPI Hook_GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT def, LPCSTR path) {
    if (section && key && _stricmp(section, "CDAUDIO") == 0 && _stricmp(key, "Confirm") == 0) {
        return 0;
    }
    return Real_GetPrivateProfileIntA(section, key, def, path);
}

static void OnMissing(const char* name, void* ctx) {
    const Logger* logger = (const Logger*)ctx;
    logger->Wrn(logger, "IAT hook missing: %s", name);
}

/* FindCdAudio enumerates empty trays and can hang forever in MCI on Win11. */
static int PatchFindCdAudio(const Logger* logger) {
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) return 0;

    BYTE* p = (BYTE*)exe + FIND_CD_AUDIO_RVA;
    if (p[0] != 0x6A || p[1] != 0xFF) {
        logger->Err(logger, "FindCdAudio prologue mismatch at %p (%.2X %.2X)", (void*)p, p[0], p[1]);
        return 0;
    }

    DWORD old = 0;
    if (!VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old)) {
        logger->Err(logger, "VirtualProtect failed (%lu)", GetLastError());
        return 0;
    }

    /* mov eax, 1; ret */
    p[0] = 0xB8;
    p[1] = 0x01;
    p[2] = 0x00;
    p[3] = 0x00;
    p[4] = 0x00;
    p[5] = 0xC3;

    VirtualProtect(p, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    logger->Suc(logger, "FindCdAudio patched @ %p (bypass)", (void*)p);
    return 1;
}

void InitCdCheck(void) {
    const Logger logger = CreateLogger("CdCheck");
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        logger.Err(&logger, "GetModuleHandleW(NULL) failed");
        return;
    }

    PatchFindCdAudio(&logger);

    Real_GetPrivateProfileIntA = GetPrivateProfileIntA;
    static const IatHookEntry hooks[] = {
        {"KERNEL32.dll", "GetPrivateProfileIntA", Hook_GetPrivateProfileIntA,
         (void**)&Real_GetPrivateProfileIntA},
    };
    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnMissing, (void*)&logger);
    logger.Suc(&logger, "CD check bypass ready (Confirm=0 hooks=%d)", n);
}
