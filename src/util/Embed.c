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

static void TrimSlash(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\\' || s[n - 1] == '/')) s[--n] = 0;
}

static int CopyOut(char* out, size_t outSz, const char* src) {
    const size_t n = strlen(src);
    if (!out || !outSz) return 1;
    if (n + 1 > outSz) return 0;
    memcpy(out, src, n + 1);
    return 1;
}

static int FileMatchesSize(const char* path, DWORD size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    const DWORD onDisk = GetFileSize(f, NULL);
    CloseHandle(f);
    return onDisk == size;
}

static int EnsureDir(const char* dir) {
    if (CreateDirectoryA(dir, NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

/* useTemp=0 → %LOCALAPPDATA%\AlivePatcher, useTemp=1 → %TEMP%\AlivePatcher */
static int UserCacheDir(char* dir, size_t dirSz, int useTemp) {
    char root[MAX_PATH];
    if (useTemp) {
        if (!GetTempPathA(MAX_PATH, root)) return 0;
    } else {
        const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", root, MAX_PATH);
        if (!n || n >= MAX_PATH) return 0;
    }
    TrimSlash(root);
    const int n = _snprintf(dir, dirSz, "%s\\AlivePatcher", root);
    if (n <= 0 || (size_t)n >= dirSz) return 0;
    return EnsureDir(dir);
}

static int EnsureAt(const char* path, DWORD size) {
    if (FileMatchesSize(path, size)) return 1;
    return EmbedExtract("WINMM_ORG", path);
}

int EmbedEnsureWinmmOrg(char* outPath, size_t outSz) {
    LogInit();
    const void* data = NULL;
    DWORD size = 0;
    if (!EmbedGet("WINMM_ORG", &data, &size)) {
        g_log.Err(&g_log, "WINMM_ORG resource missing");
        return 0;
    }
    (void)data;

    char dir[MAX_PATH];
    char path[MAX_PATH];

    /*
     * Do not write next to the proxy: Program Files is not writable for a
     * normal user, and UAC VirtualStore can fake a successful CreateFile
     * while LoadLibrary still fails.
     */
    if (UserCacheDir(dir, sizeof(dir), 0)) {
        const int n = _snprintf(path, sizeof(path), "%s\\winmm_org.dll", dir);
        if (n > 0 && (size_t)n < sizeof(path) && EnsureAt(path, size) && CopyOut(outPath, outSz, path)) {
            g_log.Suc(&g_log, "winmm_org.dll ready (%lu bytes): %s", size, path);
            return 1;
        }
        g_log.Wrn(&g_log, "AppData extract failed (%lu)", GetLastError());
    }

    if (UserCacheDir(dir, sizeof(dir), 1)) {
        const int n = _snprintf(path, sizeof(path), "%s\\winmm_org.dll", dir);
        if (n > 0 && (size_t)n < sizeof(path) && EnsureAt(path, size) && CopyOut(outPath, outSz, path)) {
            g_log.Suc(&g_log, "winmm_org.dll ready (%lu bytes): %s", size, path);
            return 1;
        }
    }

    g_log.Err(&g_log, "cannot extract winmm_org.dll to a writable path");
    return 0;
}
