#include "CdAudio.h"

#include "Embed.h"
#include "Logger.h"
#include "WinmmProxy.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

enum {
    PLAY_BGM_RVA = 0x2DCF0,
    CHUNK_BYTES = 44100,
    BUFS = 3,
};

typedef MMRESULT(WINAPI* WaveOutOpenFn)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT(WINAPI* WaveOutPrepareFn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT(WINAPI* WaveOutWriteFn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT(WINAPI* WaveOutUnprepareFn)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT(WINAPI* WaveOutResetFn)(HWAVEOUT);
typedef MMRESULT(WINAPI* WaveOutCloseFn)(HWAVEOUT);

static WaveOutOpenFn p_waveOutOpen;
static WaveOutPrepareFn p_waveOutPrepareHeader;
static WaveOutWriteFn p_waveOutWrite;
static WaveOutUnprepareFn p_waveOutUnprepareHeader;
static WaveOutResetFn p_waveOutReset;
static WaveOutCloseFn p_waveOutClose;

static volatile LONG g_stop;
static volatile LONG g_playing;
static HANDLE g_thread;
static CRITICAL_SECTION g_lock;
static char g_playName[64];
static Logger g_log;

static int ValidTrackName(const char* name) {
    if (!name || !name[0]) return 0;
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':')) return 0;
    return 1;
}

static DWORD WINAPI PlayThread(LPVOID param) {
    (void)param;

    char name[64];
    EnterCriticalSection(&g_lock);
    strncpy(name, g_playName, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    LeaveCriticalSection(&g_lock);

    const void* ogg = NULL;
    DWORD oggSize = 0;
    if (!EmbedGet(name, &ogg, &oggSize)) {
        g_log.Err(&g_log, "no embedded BGM '%s'", name);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    int channels = 0, rate = 0;
    short* pcm = NULL;
    const int samples = stb_vorbis_decode_memory(ogg, (int)oggSize, &channels, &rate, &pcm);
    if (samples <= 0 || !pcm || channels < 1 || channels > 2 || rate <= 0) {
        g_log.Err(&g_log, "decode failed: %s (samples=%d ch=%d rate=%d)", name, samples, channels, rate);
        free(pcm);
        EnterCriticalSection(&g_lock);
        g_playName[0] = 0;
        LeaveCriticalSection(&g_lock);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    WAVEFORMATEX fmt;
    ZeroMemory(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)channels;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(channels * 2);
    fmt.nAvgBytesPerSec = (DWORD)(rate * channels * 2);

    HANDLE doneEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!doneEvent) {
        free(pcm);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    HWAVEOUT wo = NULL;
    if (p_waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)doneEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        g_log.Err(&g_log, "waveOutOpen failed for %s", name);
        CloseHandle(doneEvent);
        free(pcm);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    BYTE* blocks[BUFS] = {0};
    WAVEHDR hdrs[BUFS];
    ZeroMemory(hdrs, sizeof(hdrs));
    for (int i = 0; i < BUFS; ++i) {
        blocks[i] = (BYTE*)HeapAlloc(GetProcessHeap(), 0, CHUNK_BYTES);
        if (!blocks[i]) {
            g_log.Err(&g_log, "buffer alloc failed");
            InterlockedExchange(&g_stop, 1);
            break;
        }
    }

    const DWORD totalBytes = (DWORD)samples * (DWORD)channels * 2u;
    DWORD offset = 0;
    int next = 0, queued = 0;
    g_log.Inf(&g_log, "playing %s (%d Hz, %d ch, %u bytes)", name, rate, channels, totalBytes);

    while (!InterlockedCompareExchange(&g_stop, 0, 0) && (offset < totalBytes || queued > 0)) {
        for (int i = 0; i < BUFS; ++i) {
            if ((hdrs[i].dwFlags & WHDR_PREPARED) && (hdrs[i].dwFlags & WHDR_DONE)) {
                p_waveOutUnprepareHeader(wo, &hdrs[i], sizeof(WAVEHDR));
                hdrs[i].dwFlags = 0;
                --queued;
            }
        }

        while (!InterlockedCompareExchange(&g_stop, 0, 0) && offset < totalBytes && queued < BUFS) {
            int slot = -1;
            for (int i = 0; i < BUFS; ++i) {
                const int s = (next + i) % BUFS;
                if (!(hdrs[s].dwFlags & WHDR_PREPARED)) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0 || !blocks[slot]) break;

            DWORD n = CHUNK_BYTES;
            if (offset + n > totalBytes) n = totalBytes - offset;
            n -= n % fmt.nBlockAlign;
            if (!n) break;

            memcpy(blocks[slot], (BYTE*)pcm + offset, n);
            ZeroMemory(&hdrs[slot], sizeof(WAVEHDR));
            hdrs[slot].lpData = (LPSTR)blocks[slot];
            hdrs[slot].dwBufferLength = n;
            if (p_waveOutPrepareHeader(wo, &hdrs[slot], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
                p_waveOutWrite(wo, &hdrs[slot], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                g_log.Err(&g_log, "waveOutWrite failed");
                InterlockedExchange(&g_stop, 1);
                break;
            }
            offset += n;
            next = (slot + 1) % BUFS;
            ++queued;
        }

        if (queued > 0 && !InterlockedCompareExchange(&g_stop, 0, 0))
            WaitForSingleObject(doneEvent, 50);
        else
            Sleep(5);
    }

    p_waveOutReset(wo);
    for (int i = 0; i < BUFS; ++i) {
        if (hdrs[i].dwFlags & WHDR_PREPARED) p_waveOutUnprepareHeader(wo, &hdrs[i], sizeof(WAVEHDR));
        if (blocks[i]) HeapFree(GetProcessHeap(), 0, blocks[i]);
    }
    p_waveOutClose(wo);
    CloseHandle(doneEvent);
    free(pcm);
    InterlockedExchange(&g_playing, 0);
    return 0;
}

static void StopPlayback(void) {
    InterlockedExchange(&g_stop, 1);
    if (g_thread) {
        WaitForSingleObject(g_thread, 8000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    InterlockedExchange(&g_playing, 0);
    InterlockedExchange(&g_stop, 0);
    EnterCriticalSection(&g_lock);
    g_playName[0] = 0;
    LeaveCriticalSection(&g_lock);
}

static int PlayBgm(const char* name) {
    if (!ValidTrackName(name)) {
        g_log.Err(&g_log, "bad BGM name '%s'", name ? name : "(null)");
        return 0;
    }
    const void* dummy = NULL;
    DWORD sz = 0;
    if (!EmbedGet(name, &dummy, &sz)) {
        g_log.Err(&g_log, "missing embedded BGM: %s", name);
        return 0;
    }

    EnterCriticalSection(&g_lock);
    const int same = InterlockedCompareExchange(&g_playing, 0, 0) && _stricmp(g_playName, name) == 0;
    LeaveCriticalSection(&g_lock);
    if (same) return 1;

    StopPlayback();
    EnterCriticalSection(&g_lock);
    strncpy(g_playName, name, sizeof(g_playName) - 1);
    g_playName[sizeof(g_playName) - 1] = 0;
    LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_playing, 1);
    InterlockedExchange(&g_stop, 0);
    g_thread = CreateThread(NULL, 0, PlayThread, NULL, 0, NULL);
    if (!g_thread) {
        g_log.Err(&g_log, "CreateThread failed (%lu)", GetLastError());
        InterlockedExchange(&g_playing, 0);
        return 0;
    }
    return 1;
}

static BOOL __attribute__((stdcall)) Hook_PlayBgm(char* name, unsigned int fade, char* a4, int updateFlag) {
    (void)fade;
    (void)a4;
    (void)updateFlag;
    if (!name) {
        StopPlayback();
        return TRUE;
    }
    if (!name[0]) return TRUE;
    if (_stricmp(name, "None") == 0) {
        StopPlayback();
        return TRUE;
    }
    if (PlayBgm(name)) return TRUE;
    /* CConfigSound reapplies a CD-DA alias on every toggle; keep the current ogg. */
    return TRUE;
}

static int PatchPlayBgm(void) {
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) return 0;
    BYTE* p = (BYTE*)exe + PLAY_BGM_RVA;
    if (p[0] != 0x51 || p[1] != 0x8B) {
        g_log.Err(&g_log, "PlayBgm prologue mismatch at %p (%.2X %.2X)", (void*)p, p[0], p[1]);
        return 0;
    }

    DWORD old = 0;
    if (!VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;
    const UINT_PTR dest = (UINT_PTR)&Hook_PlayBgm;
    const INT32 rel = (INT32)(dest - ((UINT_PTR)p + 5));
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    g_log.Suc(&g_log, "PlayBgm hooked @ %p -> %p", (void*)p, (void*)dest);
    return 1;
}

static int LoadWaveOut(void) {
    HMODULE org = WinmmOrgModule();
    if (!org) {
        g_log.Err(&g_log, "winmm_org.dll not loaded");
        return 0;
    }
    p_waveOutOpen = (WaveOutOpenFn)GetProcAddress(org, "waveOutOpen");
    p_waveOutPrepareHeader = (WaveOutPrepareFn)GetProcAddress(org, "waveOutPrepareHeader");
    p_waveOutWrite = (WaveOutWriteFn)GetProcAddress(org, "waveOutWrite");
    p_waveOutUnprepareHeader = (WaveOutUnprepareFn)GetProcAddress(org, "waveOutUnprepareHeader");
    p_waveOutReset = (WaveOutResetFn)GetProcAddress(org, "waveOutReset");
    p_waveOutClose = (WaveOutCloseFn)GetProcAddress(org, "waveOutClose");
    return p_waveOutOpen && p_waveOutWrite && p_waveOutPrepareHeader && p_waveOutUnprepareHeader && p_waveOutReset &&
           p_waveOutClose;
}

void InitCdAudio(void) {
    g_log = CreateLogger("CdAudio");
    InitializeCriticalSection(&g_lock);
    g_playName[0] = 0;

    if (!LoadWaveOut()) return;
    g_log.Suc(&g_log, "BGM from embedded resources");
    PatchPlayBgm();
}

void ShutdownCdAudio(void) {
    StopPlayback();
    DeleteCriticalSection(&g_lock);
}

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
