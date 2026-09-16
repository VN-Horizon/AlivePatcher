#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "WinmmProxy.h"

#include "Embed.h"
#include "Logger.h"

#include <stdio.h>
#include <string.h>

/*
 * Full winmm_org export surface.
 * Other modules in-process (e.g. nvd3dum.dll) also bind to this winmm.dll,
 * so a trimmed export set breaks GPU/driver loads.
 */
enum { WINMM_FN_COUNT = 192 };

static HMODULE g_org;
static void *g_fn[WINMM_FN_COUNT];
static Logger g_log;
static int g_ready;

static const char *const g_names[WINMM_FN_COUNT] = {
    "aux32Message",
    "auxGetDevCapsA",
    "auxGetDevCapsW",
    "auxGetNumDevs",
    "auxGetVolume",
    "auxOutMessage",
    "auxSetVolume",
    "CloseDriver",
    "DefDriverProc",
    "DriverCallback",
    "DrvGetModuleHandle",
    "GetDriverModuleHandle",
    "joy32Message",
    "joyConfigChanged",
    "joyGetDevCapsA",
    "joyGetDevCapsW",
    "joyGetNumDevs",
    "joyGetPos",
    "joyGetPosEx",
    "joyGetThreshold",
    "joyReleaseCapture",
    "joySetCapture",
    "joySetThreshold",
    "mci32Message",
    "mciDriverNotify",
    "mciDriverYield",
    "mciExecute",
    "mciFreeCommandResource",
    "mciGetCreatorTask",
    "mciGetDeviceIDA",
    "mciGetDeviceIDFromElementIDA",
    "mciGetDeviceIDFromElementIDW",
    "mciGetDeviceIDW",
    "mciGetDriverData",
    "mciGetErrorStringA",
    "mciGetErrorStringW",
    "mciGetYieldProc",
    "mciLoadCommandResource",
    "mciSendCommandA",
    "mciSendCommandW",
    "mciSendStringA",
    "mciSendStringW",
    "mciSetDriverData",
    "mciSetYieldProc",
    "mid32Message",
    "midiConnect",
    "midiDisconnect",
    "midiInAddBuffer",
    "midiInClose",
    "midiInGetDevCapsA",
    "midiInGetDevCapsW",
    "midiInGetErrorTextA",
    "midiInGetErrorTextW",
    "midiInGetID",
    "midiInGetNumDevs",
    "midiInMessage",
    "midiInOpen",
    "midiInPrepareHeader",
    "midiInReset",
    "midiInStart",
    "midiInStop",
    "midiInUnprepareHeader",
    "midiOutCacheDrumPatches",
    "midiOutCachePatches",
    "midiOutClose",
    "midiOutGetDevCapsA",
    "midiOutGetDevCapsW",
    "midiOutGetErrorTextA",
    "midiOutGetErrorTextW",
    "midiOutGetID",
    "midiOutGetNumDevs",
    "midiOutGetVolume",
    "midiOutLongMsg",
    "midiOutMessage",
    "midiOutOpen",
    "midiOutPrepareHeader",
    "midiOutReset",
    "midiOutSetVolume",
    "midiOutShortMsg",
    "midiOutUnprepareHeader",
    "midiStreamClose",
    "midiStreamOpen",
    "midiStreamOut",
    "midiStreamPause",
    "midiStreamPosition",
    "midiStreamProperty",
    "midiStreamRestart",
    "midiStreamStop",
    "mixerClose",
    "mixerGetControlDetailsA",
    "mixerGetControlDetailsW",
    "mixerGetDevCapsA",
    "mixerGetDevCapsW",
    "mixerGetID",
    "mixerGetLineControlsA",
    "mixerGetLineControlsW",
    "mixerGetLineInfoA",
    "mixerGetLineInfoW",
    "mixerGetNumDevs",
    "mixerMessage",
    "mixerOpen",
    "mixerSetControlDetails",
    "mmDrvInstall",
    "mmGetCurrentTask",
    "mmioAdvance",
    "mmioAscend",
    "mmioClose",
    "mmioCreateChunk",
    "mmioDescend",
    "mmioFlush",
    "mmioGetInfo",
    "mmioInstallIOProcA",
    "mmioInstallIOProcW",
    "mmioOpenA",
    "mmioOpenW",
    "mmioRead",
    "mmioRenameA",
    "mmioRenameW",
    "mmioSeek",
    "mmioSendMessage",
    "mmioSetBuffer",
    "mmioSetInfo",
    "mmioStringToFOURCCA",
    "mmioStringToFOURCCW",
    "mmioWrite",
    "mmsystemGetVersion",
    "mmTaskBlock",
    "mmTaskCreate",
    "mmTaskSignal",
    "mmTaskYield",
    "mod32Message",
    "mxd32Message",
    "NotifyCallbackData",
    "OpenDriver",
    "PlaySound",
    "PlaySoundA",
    "PlaySoundW",
    "SendDriverMessage",
    "sndPlaySoundA",
    "sndPlaySoundW",
    "tid32Message",
    "timeBeginPeriod",
    "timeEndPeriod",
    "timeGetDevCaps",
    "timeGetSystemTime",
    "timeGetTime",
    "timeKillEvent",
    "timeSetEvent",
    "waveInAddBuffer",
    "waveInClose",
    "waveInGetDevCapsA",
    "waveInGetDevCapsW",
    "waveInGetErrorTextA",
    "waveInGetErrorTextW",
    "waveInGetID",
    "waveInGetNumDevs",
    "waveInGetPosition",
    "waveInMessage",
    "waveInOpen",
    "waveInPrepareHeader",
    "waveInReset",
    "waveInStart",
    "waveInStop",
    "waveInUnprepareHeader",
    "waveOutBreakLoop",
    "waveOutClose",
    "waveOutGetDevCapsA",
    "waveOutGetDevCapsW",
    "waveOutGetErrorTextA",
    "waveOutGetErrorTextW",
    "waveOutGetID",
    "waveOutGetNumDevs",
    "waveOutGetPitch",
    "waveOutGetPlaybackRate",
    "waveOutGetPosition",
    "waveOutGetVolume",
    "waveOutMessage",
    "waveOutOpen",
    "waveOutPause",
    "waveOutPrepareHeader",
    "waveOutReset",
    "waveOutRestart",
    "waveOutSetPitch",
    "waveOutSetPlaybackRate",
    "waveOutSetVolume",
    "waveOutUnprepareHeader",
    "waveOutWrite",
    "wid32Message",
    "wod32Message",
    "WOW32DriverCallback",
    "WOW32ResolveMultiMediaHandle",
    "WOWAppExit",
};

HMODULE WinmmOrgModule(void) { return g_org; }

int InitWinmmProxy(void) {
    if (g_ready) return 1;
    g_log = CreateLogger("WinmmProxy");
    if (!EmbedEnsureWinmmOrg()) return 0;

    HMODULE self = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&InitWinmmProxy, &self) || !self) {
        g_log.Err(&g_log, "cannot resolve self module");
        return 0;
    }

    char dllPath[MAX_PATH];
    if (!GetModuleFileNameA(self, dllPath, MAX_PATH)) return 0;
    char *slash = strrchr(dllPath, '\\');
    if (!slash) slash = strrchr(dllPath, '/');
    if (!slash) return 0;
    slash[1] = 0;
    strncat(dllPath, "winmm_org.dll", MAX_PATH - strlen(dllPath) - 1);

    g_org = LoadLibraryA(dllPath);
    if (!g_org) {
        g_log.Err(&g_log, "LoadLibrary(%s) failed (%lu)", dllPath, GetLastError());
        return 0;
    }

    int missing = 0;
    for (int i = 0; i < WINMM_FN_COUNT; ++i) {
        g_fn[i] = (void *)GetProcAddress(g_org, g_names[i]);
        if (!g_fn[i]) {
            g_log.Wrn(&g_log, "missing export: %s", g_names[i]);
            ++missing;
        }
    }
    g_log.Suc(&g_log, "winmm_org loaded (%d exports, %d missing)", WINMM_FN_COUNT, missing);
    g_ready = 1;
    return 1;
}

__attribute__((naked)) void Proxy_aux32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[0])
    );
}

__attribute__((naked)) void Proxy_auxGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[1])
    );
}

__attribute__((naked)) void Proxy_auxGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[2])
    );
}

__attribute__((naked)) void Proxy_auxGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[3])
    );
}

__attribute__((naked)) void Proxy_auxGetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[4])
    );
}

__attribute__((naked)) void Proxy_auxOutMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[5])
    );
}

__attribute__((naked)) void Proxy_auxSetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[6])
    );
}

__attribute__((naked)) void Proxy_CloseDriver(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[7])
    );
}

__attribute__((naked)) void Proxy_DefDriverProc(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[8])
    );
}

__attribute__((naked)) void Proxy_DriverCallback(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[9])
    );
}

__attribute__((naked)) void Proxy_DrvGetModuleHandle(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[10])
    );
}

__attribute__((naked)) void Proxy_GetDriverModuleHandle(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[11])
    );
}

__attribute__((naked)) void Proxy_joy32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[12])
    );
}

__attribute__((naked)) void Proxy_joyConfigChanged(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[13])
    );
}

__attribute__((naked)) void Proxy_joyGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[14])
    );
}

__attribute__((naked)) void Proxy_joyGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[15])
    );
}

__attribute__((naked)) void Proxy_joyGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[16])
    );
}

__attribute__((naked)) void Proxy_joyGetPos(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[17])
    );
}

__attribute__((naked)) void Proxy_joyGetPosEx(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[18])
    );
}

__attribute__((naked)) void Proxy_joyGetThreshold(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[19])
    );
}

__attribute__((naked)) void Proxy_joyReleaseCapture(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[20])
    );
}

__attribute__((naked)) void Proxy_joySetCapture(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[21])
    );
}

__attribute__((naked)) void Proxy_joySetThreshold(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[22])
    );
}

__attribute__((naked)) void Proxy_mci32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[23])
    );
}

__attribute__((naked)) void Proxy_mciDriverNotify(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[24])
    );
}

__attribute__((naked)) void Proxy_mciDriverYield(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[25])
    );
}

__attribute__((naked)) void Proxy_mciExecute(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[26])
    );
}

__attribute__((naked)) void Proxy_mciFreeCommandResource(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[27])
    );
}

__attribute__((naked)) void Proxy_mciGetCreatorTask(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[28])
    );
}

__attribute__((naked)) void Proxy_mciGetDeviceIDA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[29])
    );
}

__attribute__((naked)) void Proxy_mciGetDeviceIDFromElementIDA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[30])
    );
}

__attribute__((naked)) void Proxy_mciGetDeviceIDFromElementIDW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[31])
    );
}

__attribute__((naked)) void Proxy_mciGetDeviceIDW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[32])
    );
}

__attribute__((naked)) void Proxy_mciGetDriverData(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[33])
    );
}

__attribute__((naked)) void Proxy_mciGetErrorStringA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[34])
    );
}

__attribute__((naked)) void Proxy_mciGetErrorStringW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[35])
    );
}

__attribute__((naked)) void Proxy_mciGetYieldProc(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[36])
    );
}

__attribute__((naked)) void Proxy_mciLoadCommandResource(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[37])
    );
}

__attribute__((naked)) void Proxy_mciSendCommandA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[38])
    );
}

__attribute__((naked)) void Proxy_mciSendCommandW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[39])
    );
}

__attribute__((naked)) void Proxy_mciSendStringA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[40])
    );
}

__attribute__((naked)) void Proxy_mciSendStringW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[41])
    );
}

__attribute__((naked)) void Proxy_mciSetDriverData(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[42])
    );
}

__attribute__((naked)) void Proxy_mciSetYieldProc(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[43])
    );
}

__attribute__((naked)) void Proxy_mid32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[44])
    );
}

__attribute__((naked)) void Proxy_midiConnect(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[45])
    );
}

__attribute__((naked)) void Proxy_midiDisconnect(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[46])
    );
}

__attribute__((naked)) void Proxy_midiInAddBuffer(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[47])
    );
}

__attribute__((naked)) void Proxy_midiInClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[48])
    );
}

__attribute__((naked)) void Proxy_midiInGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[49])
    );
}

__attribute__((naked)) void Proxy_midiInGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[50])
    );
}

__attribute__((naked)) void Proxy_midiInGetErrorTextA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[51])
    );
}

__attribute__((naked)) void Proxy_midiInGetErrorTextW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[52])
    );
}

__attribute__((naked)) void Proxy_midiInGetID(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[53])
    );
}

__attribute__((naked)) void Proxy_midiInGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[54])
    );
}

__attribute__((naked)) void Proxy_midiInMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[55])
    );
}

__attribute__((naked)) void Proxy_midiInOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[56])
    );
}

__attribute__((naked)) void Proxy_midiInPrepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[57])
    );
}

__attribute__((naked)) void Proxy_midiInReset(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[58])
    );
}

__attribute__((naked)) void Proxy_midiInStart(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[59])
    );
}

__attribute__((naked)) void Proxy_midiInStop(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[60])
    );
}

__attribute__((naked)) void Proxy_midiInUnprepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[61])
    );
}

__attribute__((naked)) void Proxy_midiOutCacheDrumPatches(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[62])
    );
}

__attribute__((naked)) void Proxy_midiOutCachePatches(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[63])
    );
}

__attribute__((naked)) void Proxy_midiOutClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[64])
    );
}

__attribute__((naked)) void Proxy_midiOutGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[65])
    );
}

__attribute__((naked)) void Proxy_midiOutGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[66])
    );
}

__attribute__((naked)) void Proxy_midiOutGetErrorTextA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[67])
    );
}

__attribute__((naked)) void Proxy_midiOutGetErrorTextW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[68])
    );
}

__attribute__((naked)) void Proxy_midiOutGetID(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[69])
    );
}

__attribute__((naked)) void Proxy_midiOutGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[70])
    );
}

__attribute__((naked)) void Proxy_midiOutGetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[71])
    );
}

__attribute__((naked)) void Proxy_midiOutLongMsg(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[72])
    );
}

__attribute__((naked)) void Proxy_midiOutMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[73])
    );
}

__attribute__((naked)) void Proxy_midiOutOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[74])
    );
}

__attribute__((naked)) void Proxy_midiOutPrepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[75])
    );
}

__attribute__((naked)) void Proxy_midiOutReset(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[76])
    );
}

__attribute__((naked)) void Proxy_midiOutSetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[77])
    );
}

__attribute__((naked)) void Proxy_midiOutShortMsg(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[78])
    );
}

__attribute__((naked)) void Proxy_midiOutUnprepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[79])
    );
}

__attribute__((naked)) void Proxy_midiStreamClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[80])
    );
}

__attribute__((naked)) void Proxy_midiStreamOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[81])
    );
}

__attribute__((naked)) void Proxy_midiStreamOut(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[82])
    );
}

__attribute__((naked)) void Proxy_midiStreamPause(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[83])
    );
}

__attribute__((naked)) void Proxy_midiStreamPosition(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[84])
    );
}

__attribute__((naked)) void Proxy_midiStreamProperty(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[85])
    );
}

__attribute__((naked)) void Proxy_midiStreamRestart(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[86])
    );
}

__attribute__((naked)) void Proxy_midiStreamStop(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[87])
    );
}

__attribute__((naked)) void Proxy_mixerClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[88])
    );
}

__attribute__((naked)) void Proxy_mixerGetControlDetailsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[89])
    );
}

__attribute__((naked)) void Proxy_mixerGetControlDetailsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[90])
    );
}

__attribute__((naked)) void Proxy_mixerGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[91])
    );
}

__attribute__((naked)) void Proxy_mixerGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[92])
    );
}

__attribute__((naked)) void Proxy_mixerGetID(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[93])
    );
}

__attribute__((naked)) void Proxy_mixerGetLineControlsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[94])
    );
}

__attribute__((naked)) void Proxy_mixerGetLineControlsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[95])
    );
}

__attribute__((naked)) void Proxy_mixerGetLineInfoA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[96])
    );
}

__attribute__((naked)) void Proxy_mixerGetLineInfoW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[97])
    );
}

__attribute__((naked)) void Proxy_mixerGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[98])
    );
}

__attribute__((naked)) void Proxy_mixerMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[99])
    );
}

__attribute__((naked)) void Proxy_mixerOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[100])
    );
}

__attribute__((naked)) void Proxy_mixerSetControlDetails(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[101])
    );
}

__attribute__((naked)) void Proxy_mmDrvInstall(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[102])
    );
}

__attribute__((naked)) void Proxy_mmGetCurrentTask(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[103])
    );
}

__attribute__((naked)) void Proxy_mmioAdvance(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[104])
    );
}

__attribute__((naked)) void Proxy_mmioAscend(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[105])
    );
}

__attribute__((naked)) void Proxy_mmioClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[106])
    );
}

__attribute__((naked)) void Proxy_mmioCreateChunk(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[107])
    );
}

__attribute__((naked)) void Proxy_mmioDescend(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[108])
    );
}

__attribute__((naked)) void Proxy_mmioFlush(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[109])
    );
}

__attribute__((naked)) void Proxy_mmioGetInfo(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[110])
    );
}

__attribute__((naked)) void Proxy_mmioInstallIOProcA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[111])
    );
}

__attribute__((naked)) void Proxy_mmioInstallIOProcW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[112])
    );
}

__attribute__((naked)) void Proxy_mmioOpenA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[113])
    );
}

__attribute__((naked)) void Proxy_mmioOpenW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[114])
    );
}

__attribute__((naked)) void Proxy_mmioRead(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[115])
    );
}

__attribute__((naked)) void Proxy_mmioRenameA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[116])
    );
}

__attribute__((naked)) void Proxy_mmioRenameW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[117])
    );
}

__attribute__((naked)) void Proxy_mmioSeek(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[118])
    );
}

__attribute__((naked)) void Proxy_mmioSendMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[119])
    );
}

__attribute__((naked)) void Proxy_mmioSetBuffer(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[120])
    );
}

__attribute__((naked)) void Proxy_mmioSetInfo(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[121])
    );
}

__attribute__((naked)) void Proxy_mmioStringToFOURCCA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[122])
    );
}

__attribute__((naked)) void Proxy_mmioStringToFOURCCW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[123])
    );
}

__attribute__((naked)) void Proxy_mmioWrite(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[124])
    );
}

__attribute__((naked)) void Proxy_mmsystemGetVersion(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[125])
    );
}

__attribute__((naked)) void Proxy_mmTaskBlock(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[126])
    );
}

__attribute__((naked)) void Proxy_mmTaskCreate(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[127])
    );
}

__attribute__((naked)) void Proxy_mmTaskSignal(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[128])
    );
}

__attribute__((naked)) void Proxy_mmTaskYield(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[129])
    );
}

__attribute__((naked)) void Proxy_mod32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[130])
    );
}

__attribute__((naked)) void Proxy_mxd32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[131])
    );
}

__attribute__((naked)) void Proxy_NotifyCallbackData(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[132])
    );
}

__attribute__((naked)) void Proxy_OpenDriver(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[133])
    );
}

__attribute__((naked)) void Proxy_PlaySound(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[134])
    );
}

__attribute__((naked)) void Proxy_PlaySoundA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[135])
    );
}

__attribute__((naked)) void Proxy_PlaySoundW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[136])
    );
}

__attribute__((naked)) void Proxy_SendDriverMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[137])
    );
}

__attribute__((naked)) void Proxy_sndPlaySoundA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[138])
    );
}

__attribute__((naked)) void Proxy_sndPlaySoundW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[139])
    );
}

__attribute__((naked)) void Proxy_tid32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[140])
    );
}

__attribute__((naked)) void Proxy_timeBeginPeriod(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[141])
    );
}

__attribute__((naked)) void Proxy_timeEndPeriod(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[142])
    );
}

__attribute__((naked)) void Proxy_timeGetDevCaps(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[143])
    );
}

__attribute__((naked)) void Proxy_timeGetSystemTime(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[144])
    );
}

__attribute__((naked)) void Proxy_timeGetTime(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[145])
    );
}

__attribute__((naked)) void Proxy_timeKillEvent(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[146])
    );
}

__attribute__((naked)) void Proxy_timeSetEvent(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[147])
    );
}

__attribute__((naked)) void Proxy_waveInAddBuffer(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[148])
    );
}

__attribute__((naked)) void Proxy_waveInClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[149])
    );
}

__attribute__((naked)) void Proxy_waveInGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[150])
    );
}

__attribute__((naked)) void Proxy_waveInGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[151])
    );
}

__attribute__((naked)) void Proxy_waveInGetErrorTextA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[152])
    );
}

__attribute__((naked)) void Proxy_waveInGetErrorTextW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[153])
    );
}

__attribute__((naked)) void Proxy_waveInGetID(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[154])
    );
}

__attribute__((naked)) void Proxy_waveInGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[155])
    );
}

__attribute__((naked)) void Proxy_waveInGetPosition(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[156])
    );
}

__attribute__((naked)) void Proxy_waveInMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[157])
    );
}

__attribute__((naked)) void Proxy_waveInOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[158])
    );
}

__attribute__((naked)) void Proxy_waveInPrepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[159])
    );
}

__attribute__((naked)) void Proxy_waveInReset(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[160])
    );
}

__attribute__((naked)) void Proxy_waveInStart(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[161])
    );
}

__attribute__((naked)) void Proxy_waveInStop(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[162])
    );
}

__attribute__((naked)) void Proxy_waveInUnprepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[163])
    );
}

__attribute__((naked)) void Proxy_waveOutBreakLoop(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[164])
    );
}

__attribute__((naked)) void Proxy_waveOutClose(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[165])
    );
}

__attribute__((naked)) void Proxy_waveOutGetDevCapsA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[166])
    );
}

__attribute__((naked)) void Proxy_waveOutGetDevCapsW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[167])
    );
}

__attribute__((naked)) void Proxy_waveOutGetErrorTextA(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[168])
    );
}

__attribute__((naked)) void Proxy_waveOutGetErrorTextW(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[169])
    );
}

__attribute__((naked)) void Proxy_waveOutGetID(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[170])
    );
}

__attribute__((naked)) void Proxy_waveOutGetNumDevs(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[171])
    );
}

__attribute__((naked)) void Proxy_waveOutGetPitch(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[172])
    );
}

__attribute__((naked)) void Proxy_waveOutGetPlaybackRate(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[173])
    );
}

__attribute__((naked)) void Proxy_waveOutGetPosition(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[174])
    );
}

__attribute__((naked)) void Proxy_waveOutGetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[175])
    );
}

__attribute__((naked)) void Proxy_waveOutMessage(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[176])
    );
}

__attribute__((naked)) void Proxy_waveOutOpen(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[177])
    );
}

__attribute__((naked)) void Proxy_waveOutPause(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[178])
    );
}

__attribute__((naked)) void Proxy_waveOutPrepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[179])
    );
}

__attribute__((naked)) void Proxy_waveOutReset(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[180])
    );
}

__attribute__((naked)) void Proxy_waveOutRestart(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[181])
    );
}

__attribute__((naked)) void Proxy_waveOutSetPitch(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[182])
    );
}

__attribute__((naked)) void Proxy_waveOutSetPlaybackRate(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[183])
    );
}

__attribute__((naked)) void Proxy_waveOutSetVolume(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[184])
    );
}

__attribute__((naked)) void Proxy_waveOutUnprepareHeader(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[185])
    );
}

__attribute__((naked)) void Proxy_waveOutWrite(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[186])
    );
}

__attribute__((naked)) void Proxy_wid32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[187])
    );
}

__attribute__((naked)) void Proxy_wod32Message(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[188])
    );
}

__attribute__((naked)) void Proxy_WOW32DriverCallback(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[189])
    );
}

__attribute__((naked)) void Proxy_WOW32ResolveMultiMediaHandle(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[190])
    );
}

__attribute__((naked)) void Proxy_WOWAppExit(void) {
    __asm__ __volatile__(
        "jmp *%0"
        :
        : "m"(g_fn[191])
    );
}
