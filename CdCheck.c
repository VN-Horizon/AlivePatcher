#include "CdCheck.h"

#include "IatHook.h"
#include "Logger.h"

#include <ctype.h>
#include <string.h>

enum { FIND_CD_AUDIO_RVA = 0x31180 }; /* VA 0x431180 */

static UINT(WINAPI* Real_GetPrivateProfileIntA)(LPCSTR, LPCSTR, INT, LPCSTR);
static UINT(WINAPI* Real_GetDriveTypeA)(LPCSTR);
static DWORD(WINAPI* Real_GetFileAttributesA)(LPCSTR);

static UINT WINAPI Hook_GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT def, LPCSTR path) {
    if (section && key && _stricmp(section, "CDAUDIO") == 0 && _stricmp(key, "Confirm") == 0) {
        return 0;
    }
    return Real_GetPrivateProfileIntA(section, key, def, path);
}

/*
 * Never open a volume. Empty optical trays used to hang CConfigSaveLoad
 * (tab 3) after GetDriveTypeA==DRIVE_CDROM; treat CdRom devices as absent.
 */
static UINT WINAPI Hook_GetDriveTypeA(LPCSTR root) {
    if (!root || !root[0] || root[1] != ':') {
        return Real_GetDriveTypeA ? Real_GetDriveTypeA(root) : DRIVE_NO_ROOT_DIR;
    }

    const int letter = toupper((unsigned char)root[0]);
    if (letter < 'A' || letter > 'Z') {
        return Real_GetDriveTypeA ? Real_GetDriveTypeA(root) : DRIVE_NO_ROOT_DIR;
    }
    if (letter == 'A' || letter == 'B') return DRIVE_NO_ROOT_DIR;

    const DWORD present = GetLogicalDrives();
    if (!(present & (1u << (letter - 'A')))) return DRIVE_NO_ROOT_DIR;

    char dos[3] = {(char)letter, ':', 0};
    char dev[MAX_PATH];
    if (!QueryDosDeviceA(dos, dev, MAX_PATH)) return DRIVE_NO_ROOT_DIR;
    for (char* p = dev; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    }
    if (strstr(dev, "CDROM")) return DRIVE_NO_ROOT_DIR;
    if (strstr(dev, "FLOPPY")) return DRIVE_REMOVABLE;
    if (strstr(dev, "LANMAN") || strstr(dev, "MUP")) return DRIVE_REMOTE;
    return DRIVE_FIXED;
}

static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR path) {
    if (path && path[0] && path[1] == ':') {
        char root[4] = {(char)toupper((unsigned char)path[0]), ':', '\\', 0};
        const UINT type = Hook_GetDriveTypeA(root);
        if (type == DRIVE_CDROM || type == DRIVE_REMOTE || type == DRIVE_NO_ROOT_DIR) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_FILE_ATTRIBUTES;
        }
    }
    return Real_GetFileAttributesA ? Real_GetFileAttributesA(path) : INVALID_FILE_ATTRIBUTES;
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

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    PatchFindCdAudio(&logger);

    Real_GetPrivateProfileIntA = GetPrivateProfileIntA;
    Real_GetDriveTypeA = GetDriveTypeA;
    Real_GetFileAttributesA = GetFileAttributesA;
    static const IatHookEntry hooks[] = {
        {"KERNEL32.dll", "GetPrivateProfileIntA", Hook_GetPrivateProfileIntA,
         (void**)&Real_GetPrivateProfileIntA},
        {"KERNEL32.dll", "GetDriveTypeA", Hook_GetDriveTypeA, (void**)&Real_GetDriveTypeA},
        {"KERNEL32.dll", "GetFileAttributesA", Hook_GetFileAttributesA, (void**)&Real_GetFileAttributesA},
    };
    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnMissing, (void*)&logger);
    logger.Suc(&logger, "CD check bypass ready (Confirm=0 hooks=%d)", n);
}
