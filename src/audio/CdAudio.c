#include "CdAudio.h"

#include "Embed.h"
#include "Logger.h"

#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>

#include <string.h>
#include <stdlib.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

enum { PLAY_BGM_RVA = 0x2DCF0 };

/* Own WASAPI session so PlayVoice's WAVE mixer fade (sub_42DFD0) cannot duck BGM.
   Vista+ maps MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT onto the default session. */
static const GUID kBgmSession = {0xa11ce0b6, 0x4d7f, 0x4e2a, {0x9c, 0x51, 0x2b, 0x8e, 0x6f, 0x0d, 0x7c, 0x19}};

static volatile LONG g_stop;
static volatile LONG g_playing;
static HANDLE g_thread;
static CRITICAL_SECTION g_lock;
static char g_playName[64];
static Logger g_log;

static const char* const kTrackTitles[30] = {
    "ALIVE",
    "Teriru Jingle",
    "HEAVEN",
    "LIKE or LOVE",
    "Inocent CAT",
    "My fear LADY",
    "Crime and punishment",
    "Pure",
    "Undead",
    "Poker Face",
    "The god of death",
    "Love that clings",
    "Face off",
    "Holy Wing",
    "BIRD SONGS",
    "Sister",
    "Afternoon tea",
    "Hearless",
    "Sandglass",
    "The Redemption",
    "Dreaming",
    "HEAVEN - orgole ver",
    "Heartful morning",
    "Memorial Day",
    "WHITE SNOW",
    "ZOOM - type R",
    "The Crack Of Doom",
    "Alive Renewal",
    "Duel !",
    "KEEP OFF - type R",
};

static const char* TrackTitle(const char* name) {
    if (!name || (name[0] != 'M' && name[0] != 'm')) return NULL;
    if (name[1] < '0' || name[1] > '9' || name[2] < '0' || name[2] > '9' || name[3]) return NULL;
    const int n = (name[1] - '0') * 10 + (name[2] - '0');
    if (n < 1 || n > 30) return NULL;
    return kTrackTitles[n - 1];
}

static int ValidTrackName(const char* name) {
    if (!name || !name[0]) return 0;
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':')) return 0;
    return 1;
}

static void PlayPcm(const short* pcm, int samples, int channels, int rate) {
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioClient* ac = NULL;
    IAudioRenderClient* rc = NULL;
    IAudioSessionControl2* sess = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&en);
    if (FAILED(hr) || !en) {
        g_log.Err(&g_log, "MMDeviceEnumerator failed (%08lX)", (unsigned long)hr);
        return;
    }
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (FAILED(hr) || !dev) {
        g_log.Err(&g_log, "GetDefaultAudioEndpoint failed (%08lX)", (unsigned long)hr);
        goto done;
    }
    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&ac);
    if (FAILED(hr) || !ac) {
        g_log.Err(&g_log, "IAudioClient activate failed (%08lX)", (unsigned long)hr);
        goto done;
    }

    WAVEFORMATEX fmt;
    ZeroMemory(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)channels;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(channels * 2);
    fmt.nAvgBytesPerSec = (DWORD)(rate * channels * 2);

    const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                        AUDCLNT_STREAMFLAGS_NOPERSIST;
    hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, flags, 5000000, 0, &fmt, &kBgmSession);
    if (FAILED(hr)) {
        g_log.Err(&g_log, "IAudioClient Initialize failed (%08lX)", (unsigned long)hr);
        goto done;
    }

    if (SUCCEEDED(IAudioClient_GetService(ac, &IID_IAudioSessionControl2, (void**)&sess)) && sess) {
        IAudioSessionControl2_SetDuckingPreference(sess, TRUE);
    }

    hr = IAudioClient_GetService(ac, &IID_IAudioRenderClient, (void**)&rc);
    if (FAILED(hr) || !rc) {
        g_log.Err(&g_log, "IAudioRenderClient failed (%08lX)", (unsigned long)hr);
        goto done;
    }

    UINT32 bufFrames = 0;
    hr = IAudioClient_GetBufferSize(ac, &bufFrames);
    if (FAILED(hr) || !bufFrames) {
        g_log.Err(&g_log, "GetBufferSize failed (%08lX)", (unsigned long)hr);
        goto done;
    }

    hr = IAudioClient_Start(ac);
    if (FAILED(hr)) {
        g_log.Err(&g_log, "IAudioClient Start failed (%08lX)", (unsigned long)hr);
        goto done;
    }

    const UINT32 total = (UINT32)samples;
    const UINT32 frameBytes = (UINT32)channels * 2u;
    UINT32 written = 0;
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        UINT32 padding = 0;
        hr = IAudioClient_GetCurrentPadding(ac, &padding);
        if (FAILED(hr)) break;

        if (written >= total) {
            if (padding == 0) break;
            Sleep(15);
            continue;
        }

        UINT32 n = bufFrames - padding;
        const UINT32 left = total - written;
        if (n > left) n = left;
        if (!n) {
            Sleep(8);
            continue;
        }

        BYTE* dest = NULL;
        hr = IAudioRenderClient_GetBuffer(rc, n, &dest);
        if (FAILED(hr) || !dest) break;
        memcpy(dest, (const BYTE*)pcm + (size_t)written * frameBytes, (size_t)n * frameBytes);
        hr = IAudioRenderClient_ReleaseBuffer(rc, n, 0);
        if (FAILED(hr)) break;
        written += n;
    }

    IAudioClient_Stop(ac);

done:
    if (sess) IAudioSessionControl2_Release(sess);
    if (rc) IAudioRenderClient_Release(rc);
    if (ac) IAudioClient_Release(ac);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
}

static DWORD WINAPI PlayThread(LPVOID param) {
    (void)param;

    const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const int com = SUCCEEDED(co);

    char name[64];
    EnterCriticalSection(&g_lock);
    strncpy(name, g_playName, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    LeaveCriticalSection(&g_lock);

    const void* ogg = NULL;
    DWORD oggSize = 0;
    if (!EmbedGet(name, &ogg, &oggSize)) {
        g_log.Err(&g_log, "no embedded BGM '%s'", name);
        goto done;
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
        goto done;
    }

    const char* title = TrackTitle(name);
    if (title) {
        g_log.Wrn(&g_log, "♫ %s (%s)", title, name);
    } else {
        g_log.Wrn(&g_log, "♫ %s", name);
    }
    PlayPcm(pcm, samples, channels, rate);
    free(pcm);

done:
    InterlockedExchange(&g_playing, 0);
    if (com) CoUninitialize();
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
    g_log.Suc(&g_log, "PlayBgm hooked @ %p -> %p", (void*)p, (void*)&Hook_PlayBgm);
    return 1;
}

void InitCdAudio(void) {
    g_log = CreateLogger("BGM");
    InitializeCriticalSection(&g_lock);
    g_playName[0] = 0;

    g_log.Suc(&g_log, "BGM from embedded resources (WASAPI)");
    PatchPlayBgm();
}

void ShutdownCdAudio(void) {
    StopPlayback();
    DeleteCriticalSection(&g_lock);
}

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
