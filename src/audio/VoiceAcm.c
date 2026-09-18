#include "VoiceAcm.h"

#include "Embed.h"
#include "IatHook.h"
#include "Logger.h"
#include "VoiceAcmInstallScript.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define VORBIS_REG "msacm.vorbis"
#define VORBIS_FILE "vorbis.acm"

typedef HANDLE HACMDRIVERID;
typedef HANDLE HACMDRIVER;
typedef HACMDRIVER* LPHACMDRIVER;
typedef MMRESULT(WINAPI* AcmDriverAddAFn)(HACMDRIVERID*, HINSTANCE, LPARAM, DWORD, DWORD);
typedef MMRESULT(WINAPI* AcmDriverOpenFn)(LPHACMDRIVER, HACMDRIVERID, DWORD);
typedef MMRESULT(WINAPI* AcmDriverCloseFn)(HACMDRIVER, DWORD);

static HMODULE(WINAPI* Real_LoadLibraryA)(LPCSTR);

static char g_gameDir[MAX_PATH];
static char g_exePath[MAX_PATH];
static char g_vorbisPath[MAX_PATH];
static Logger g_log;
static volatile LONG g_restarting;

static int ResolvePaths(void) {
    const DWORD n = GetModuleFileNameA(NULL, g_exePath, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    strncpy(g_gameDir, g_exePath, sizeof(g_gameDir) - 1);
    g_gameDir[sizeof(g_gameDir) - 1] = 0;
    char* slash = strrchr(g_gameDir, '\\');
    if (!slash) slash = strrchr(g_gameDir, '/');
    if (!slash) return 0;
    *slash = 0;
    _snprintf(g_vorbisPath, sizeof(g_vorbisPath), "%s\\%s", g_gameDir, VORBIS_FILE);
    return 1;
}

static int EnsureSource(void) {
    const void* data = NULL;
    DWORD size = 0;
    if (!EmbedGet("VORBIS_ACM", &data, &size)) return 0;

    HANDLE f = CreateFileA(g_vorbisPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        const DWORD onDisk = GetFileSize(f, NULL);
        CloseHandle(f);
        if (onDisk == size) return 1;
    }
    return EmbedExtract("VORBIS_ACM", g_vorbisPath);
}

/* 32-bit process: SystemDirectory is SysWOW64; default registry view is 32-bit Drivers32. */
static int IsVorbisInstalled(void) {
    char sys[MAX_PATH], path[MAX_PATH];
    if (!GetSystemDirectoryA(sys, MAX_PATH)) return 0;
    _snprintf(path, sizeof(path), "%s\\%s", sys, VORBIS_FILE);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        g_log.Inf(&g_log, "install check: file=0 (%s)", path);
        return 0;
    }

    HKEY key = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        g_log.Inf(&g_log, "install check: file=1 reg=0");
        return 0;
    }
    char buf[MAX_PATH];
    DWORD type = 0, cb = sizeof(buf);
    const LONG qr = RegQueryValueExA(key, VORBIS_REG, NULL, &type, (LPBYTE)buf, &cb);
    RegCloseKey(key);
    const int regOk = qr == ERROR_SUCCESS && type == REG_SZ && _stricmp(buf, VORBIS_FILE) == 0;
    g_log.Inf(&g_log, "install check: file=1 reg=%d", regOk);
    return regOk;
}

static DWORD WINAPI InstallWorker(LPVOID param) {
    (void)param;
    Sleep(600);

    char temp[MAX_PATH], ps1[MAX_PATH], psExe[MAX_PATH], cmd[MAX_PATH * 4];
    if (!GetTempPathA(MAX_PATH, temp)) ExitProcess(1);
    _snprintf(ps1, sizeof(ps1), "%salive_install_vorbis.ps1", temp);

    HANDLE f = CreateFileA(ps1, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) ExitProcess(1);
    DWORD written = 0;
    WriteFile(f, kVorbisInstallPs1, (DWORD)(sizeof(kVorbisInstallPs1) - 1), &written, NULL);
    CloseHandle(f);

    char winDir[MAX_PATH];
    GetWindowsDirectoryA(winDir, MAX_PATH);
    _snprintf(psExe, sizeof(psExe), "%s\\SysWOW64\\WindowsPowerShell\\v1.0\\powershell.exe", winDir);
    if (GetFileAttributesA(psExe) == INVALID_FILE_ATTRIBUTES)
        _snprintf(psExe, sizeof(psExe), "%s\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", winDir);

    _snprintf(cmd, sizeof(cmd),
              "\"%s\" -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Source \"%s\" -RestartExe \"%s\"", psExe, ps1,
              g_vorbisPath, g_exePath);

    MessageBoxW(NULL,
                L"没有检测到 Vorbis 语音编码器, 需要安装才可以正常播放角色语音。\n\n"
                L"AlivePatcher 将会申请管理员权限并安装 Vorbis 语音编码器, "
                L"然后重启游戏.\n\n点击确定, 然后点击 UAC 中的确定.",
                L"AlivePatcher", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, g_gameDir, &si, &pi)) {
        MessageBoxW(NULL, L"启动安装程序失败.", L"AlivePatcher", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(0);
    return 0;
}

static void ScheduleInstall(void) {
    InterlockedExchange(&g_restarting, 1);
    HANDLE th = CreateThread(NULL, 0, InstallWorker, NULL, 0, NULL);
    if (!th) {
        InterlockedExchange(&g_restarting, 0);
        return;
    }
    CloseHandle(th);
    g_log.Inf(&g_log, "vorbis not installed; UAC installer scheduled");
}

int VoiceAcmIsRestarting(void) {
    return InterlockedCompareExchange(&g_restarting, 0, 0) != 0;
}

void InitVoiceAcm(void) {
    g_log = CreateLogger("VoiceAcm");
    g_gameDir[0] = g_exePath[0] = g_vorbisPath[0] = 0;
    InterlockedExchange(&g_restarting, 0);

    if (!ResolvePaths() || !EnsureSource()) {
        g_log.Err(&g_log, "path/source setup failed");
        return;
    }

    if (!IsVorbisInstalled()) {
        ScheduleInstall();
        return;
    }
    g_log.Suc(&g_log, "vorbis.acm installed (system)");
}
