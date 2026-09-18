#include "CdAudio.h"

#include "Embed.h"
#include "Logger.h"

#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <xaudio2.h>

#include <string.h>
#include <stdlib.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

enum { PLAY_BGM_RVA = 0x2DCF0 };

typedef HRESULT(WINAPI* XAudio2CreateFn)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

static XAudio2CreateFn p_XAudio2Create;
static HMODULE g_xaudio;
static IXAudio2* g_xa2;
static IXAudio2MasteringVoice* g_master;

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

static void TeardownXAudio(void) {
    if (g_master) {
        IXAudio2MasteringVoice_DestroyVoice(g_master);
        g_master = NULL;
    }
    if (g_xa2) {
        IXAudio2_Release(g_xa2);
        g_xa2 = NULL;
    }
}

static int EnsureXAudio(void) {
    if (g_xa2) return 1;
    if (!p_XAudio2Create) return 0;

    /* Processor 0 = XAUDIO2_USE_DEFAULT_PROCESSOR (modern xaudio2_9). */
    HRESULT hr = p_XAudio2Create(&g_xa2, 0, 0);
    if (FAILED(hr) || !g_xa2) {
        g_log.Err(&g_log, "XAudio2Create failed (%08lX)", (unsigned long)hr);
        g_xa2 = NULL;
        return 0;
    }
    hr = IXAudio2_CreateMasteringVoice(g_xa2, &g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL,
                                       NULL, AudioCategory_GameMedia);
    if (FAILED(hr) || !g_master) {
        g_log.Err(&g_log, "CreateMasteringVoice failed (%08lX)", (unsigned long)hr);
        TeardownXAudio();
        return 0;
    }
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

    if (!EnsureXAudio()) {
        free(pcm);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    IXAudio2SourceVoice* voice = NULL;
    HRESULT hr = IXAudio2_CreateSourceVoice(g_xa2, &voice, &fmt, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr) || !voice) {
        g_log.Err(&g_log, "CreateSourceVoice failed (%08lX)", (unsigned long)hr);
        free(pcm);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    const UINT32 totalBytes = (UINT32)samples * (UINT32)channels * 2u;
    XAUDIO2_BUFFER buf;
    ZeroMemory(&buf, sizeof(buf));
    buf.AudioBytes = totalBytes;
    buf.pAudioData = (const BYTE*)pcm;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    hr = IXAudio2SourceVoice_SubmitSourceBuffer(voice, &buf, NULL);
    if (FAILED(hr)) {
        g_log.Err(&g_log, "SubmitSourceBuffer failed (%08lX)", (unsigned long)hr);
        IXAudio2SourceVoice_DestroyVoice(voice);
        free(pcm);
        InterlockedExchange(&g_playing, 0);
        return 0;
    }

    g_log.Inf(&g_log, "playing %s (%d Hz, %d ch, %u bytes)", name, rate, channels, totalBytes);
    IXAudio2SourceVoice_Start(voice, 0, XAUDIO2_COMMIT_NOW);

    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        XAUDIO2_VOICE_STATE st;
        IXAudio2SourceVoice_GetState(voice, &st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) break;
        Sleep(20);
    }

    IXAudio2SourceVoice_Stop(voice, 0, XAUDIO2_COMMIT_NOW);
    IXAudio2SourceVoice_FlushSourceBuffers(voice);
    IXAudio2SourceVoice_DestroyVoice(voice);
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
    g_log.Suc(&g_log, "PlayBgm hooked @ %p -> %p", (void*)p, (void*)&Hook_PlayBgm);
    return 1;
}

static int LoadXAudio(void) {
    g_xaudio = LoadLibraryW(L"xaudio2_9.dll");
    if (!g_xaudio) g_xaudio = LoadLibraryW(L"xaudio2_8.dll");
    if (!g_xaudio) {
        g_log.Err(&g_log, "xaudio2_9/8.dll not found (%lu)", GetLastError());
        return 0;
    }
    p_XAudio2Create = (XAudio2CreateFn)GetProcAddress(g_xaudio, "XAudio2Create");
    if (!p_XAudio2Create) {
        g_log.Err(&g_log, "XAudio2Create missing");
        return 0;
    }
    return 1;
}

void InitCdAudio(void) {
    g_log = CreateLogger("CdAudio");
    InitializeCriticalSection(&g_lock);
    g_playName[0] = 0;

    if (!LoadXAudio()) return;
    g_log.Suc(&g_log, "BGM from embedded resources (XAudio2)");
    PatchPlayBgm();
}

void ShutdownCdAudio(void) {
    StopPlayback();
    TeardownXAudio();
    DeleteCriticalSection(&g_lock);
}

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
