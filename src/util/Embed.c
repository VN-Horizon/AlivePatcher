#include "Embed.h"

#include "Logger.h"

#include <stdio.h>
#include <string.h>

static HMODULE g_self;
static Logger g_log;
static int g_logReady;

static void LogInit(void) {
    if (g_logReady) return;
    g_log = CreateLogger("Embed");
    g_logReady = 1;
}

void EmbedSetModule(HMODULE self) {
    g_self = self;
}

int EmbedGet(const char* name, const void** data, DWORD* size) {
    if (!g_self || !name || !data || !size) return 0;
    HRSRC res = FindResourceA(g_self, name, RT_RCDATA);
    if (!res) return 0;
    HGLOBAL glob = LoadResource(g_self, res);
    if (!glob) return 0;
    const void* p = LockResource(glob);
    const DWORD n = SizeofResource(g_self, res);
    if (!p || !n) return 0;
    *data = p;
    *size = n;
    return 1;
}

int EmbedExtract(const char* name, const char* path) {
    LogInit();
    const void* data = NULL;
    DWORD size = 0;
    if (!EmbedGet(name, &data, &size) || !path) {
        g_log.Err(&g_log, "missing resource '%s'", name ? name : "(null)");
        return 0;
    }
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        g_log.Err(&g_log, "create %s failed (%lu)", path, GetLastError());
        return 0;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(f, data, size, &written, NULL);
    CloseHandle(f);
    if (!ok || written != size) {
        g_log.Err(&g_log, "write %s failed (%lu)", path, GetLastError());
        DeleteFileA(path);
        return 0;
    }
    return 1;
}

static int ModuleDir(char* out, size_t outSz) {
    if (!g_self || !out || outSz < 4) return 0;
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (!slash) return 0;
    *slash = 0;
    if (strlen(path) + 1 > outSz) return 0;
    memcpy(out, path, strlen(path) + 1);
    return 1;
}

int EmbedExtractBesideSelf(const char* name, const char* fileName, char* outPath, size_t outSz) {
    char dir[MAX_PATH];
    if (!ModuleDir(dir, sizeof(dir)) || !fileName) return 0;
    char path[MAX_PATH];
    const int n = _snprintf(path, sizeof(path), "%s\\%s", dir, fileName);
    if (n <= 0 || (size_t)n >= sizeof(path)) return 0;
    if (!EmbedExtract(name, path)) return 0;
    if (outPath && outSz) {
        strncpy(outPath, path, outSz - 1);
        outPath[outSz - 1] = 0;
    }
    return 1;
}

int EmbedEnsureWinmmOrg(void) {
    LogInit();
    char dir[MAX_PATH];
    if (!ModuleDir(dir, sizeof(dir))) return 0;

    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s\\winmm_org.dll", dir);

    const void* data = NULL;
    DWORD size = 0;
    if (!EmbedGet("WINMM_ORG", &data, &size)) {
        g_log.Err(&g_log, "WINMM_ORG resource missing");
        return 0;
    }

    /* Skip rewrite when on-disk copy already matches. */
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        const DWORD onDisk = GetFileSize(f, NULL);
        CloseHandle(f);
        if (onDisk == size) {
            g_log.Inf(&g_log, "winmm_org.dll present (%lu bytes)", size);
            return 1;
        }
    }

    if (!EmbedExtract("WINMM_ORG", path)) return 0;
    g_log.Suc(&g_log, "extracted winmm_org.dll (%lu bytes)", size);
    return 1;
}
