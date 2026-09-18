#include "CreditsOverlay.h"

#include "Embed.h"
#include "InlineHook.h"
#include "Logger.h"

#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <wincodec.h>

enum {
    RVA_HOME = 0x24CD20, /* VA 0x64CD20 START.PSD title/home loop */
    HOME_STOLEN = 5,
    MARGIN = 12,
    TRACK_MS = 50,
};

typedef char*(__attribute__((thiscall)) *HomeFn)(void* this);

static const BYTE kHomeExpect[HOME_STOLEN] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
static const char kClass[] = "AliveCreditsOverlay";

static HomeFn Real_Home;
static Logger g_log;
static HWND g_overlay;
static HWND g_game;
static HBITMAP g_bmp;
static HDC g_memdc;
static HGDIOBJ g_oldbm;
static int g_w;
static int g_h;
static HANDLE g_thread;
static DWORD g_tid;
static volatile LONG g_wantShow;
static volatile LONG g_stop;

static BOOL CALLBACK EnumGameWnd(HWND h, LPARAM lp) {
    struct Pair {
        HWND best;
        LONG area;
    }* ctx = (struct Pair*)lp;

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (h == g_overlay) return TRUE;
    if (h == GetConsoleWindow()) return TRUE;
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    if (GetWindow(h, GW_OWNER)) return TRUE;

    const LONG ex = GetWindowLongA(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;

    char cls[64];
    if (GetClassNameA(h, cls, (int)sizeof(cls)) && lstrcmpA(cls, kClass) == 0) return TRUE;

    RECT rc;
    if (!GetClientRect(h, &rc)) return TRUE;
    const LONG area = rc.right * rc.bottom;
    if (area < 320 * 240) return TRUE;
    if (area > ctx->area) {
        ctx->area = area;
        ctx->best = h;
    }
    return TRUE;
}

static HWND GameWindow(void) {
    struct {
        HWND best;
        LONG area;
    } ctx = {NULL, -1};
    EnumWindows(EnumGameWnd, (LPARAM)&ctx);
    return ctx.best;
}

static int LoadPng(void) {
    const void* data = NULL;
    DWORD size = 0;
    if (!EmbedGet("CREDITS", &data, &size) || !data || !size) {
        g_log.Err(&g_log, "credits.png resource missing");
        return 0;
    }

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return 0;
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        return 0;
    }
    memcpy(locked, data, size);
    GlobalUnlock(mem);

    IStream* stream = NULL;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream)) || !stream) {
        GlobalFree(mem);
        return 0;
    }

    IWICImagingFactory* fac = NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    int ok = 0;

    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                (void**)&fac))) {
        g_log.Err(&g_log, "WIC factory failed");
        goto done;
    }
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(fac, stream, NULL, WICDecodeMetadataCacheOnLoad, &dec))) {
        g_log.Err(&g_log, "PNG decode failed");
        goto done;
    }
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(fac, &conv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppPBGRA,
                                              WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
        goto done;
    }

    UINT w = 0, h = 0;
    if (FAILED(IWICBitmapSource_GetSize((IWICBitmapSource*)conv, &w, &h)) || !w || !h || w > 4096 || h > 4096) {
        goto done;
    }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) goto done;
    if (FAILED(IWICBitmapSource_CopyPixels((IWICBitmapSource*)conv, NULL, w * 4, w * h * 4, (BYTE*)bits))) {
        DeleteObject(bmp);
        goto done;
    }

    HDC memdc = CreateCompatibleDC(NULL);
    if (!memdc) {
        DeleteObject(bmp);
        goto done;
    }
    g_oldbm = SelectObject(memdc, bmp);
    g_bmp = bmp;
    g_memdc = memdc;
    g_w = (int)w;
    g_h = (int)h;
    ok = 1;
    g_log.Suc(&g_log, "credits.png %dx%d", g_w, g_h);

done:
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (fac) IWICImagingFactory_Release(fac);
    if (stream) stream->lpVtbl->Release(stream);
    return ok;
}

static void PlaceOverlay(HWND game) {
    if (!g_overlay || !g_memdc || !game) return;

    RECT client;
    if (!GetClientRect(game, &client)) return;
    POINT br = {client.right, client.bottom};
    POINT tl = {0, 0};
    ClientToScreen(game, &br);
    ClientToScreen(game, &tl);

    int x = br.x - g_w - MARGIN;
    int y = br.y - g_h - MARGIN;
    if (x < tl.x) x = tl.x;
    if (y < tl.y) y = tl.y;

    POINT dst = {x, y};
    POINT src = {0, 0};
    SIZE sz = {g_w, g_h};
    BLENDFUNCTION bf;
    ZeroMemory(&bf, sizeof(bf));
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    HDC screen = GetDC(NULL);
    UpdateLayeredWindow(g_overlay, screen, &dst, &sz, g_memdc, &src, 0, &bf, ULW_ALPHA);
    if (screen) ReleaseDC(NULL, screen);

    const HWND fg = GetForegroundWindow();
    const HWND after = (fg == game || fg == g_overlay) ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(g_overlay, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void SyncOverlay(void) {
    if (!g_overlay) return;

    if (!InterlockedCompareExchange(&g_wantShow, 0, 0)) {
        if (IsWindowVisible(g_overlay)) ShowWindow(g_overlay, SW_HIDE);
        g_game = NULL;
        return;
    }

    HWND game = GameWindow();
    if (!game || !IsWindowVisible(game) || IsIconic(game)) {
        if (IsWindowVisible(g_overlay)) ShowWindow(g_overlay, SW_HIDE);
        g_game = NULL;
        return;
    }

    g_game = game;
    PlaceOverlay(game);
    if (!IsWindowVisible(g_overlay)) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        SyncOverlay();
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_DESTROY) {
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void FreeBitmap(void) {
    if (g_memdc) {
        if (g_oldbm) SelectObject(g_memdc, g_oldbm);
        DeleteDC(g_memdc);
    }
    if (g_bmp) DeleteObject(g_bmp);
    g_memdc = NULL;
    g_bmp = NULL;
    g_oldbm = NULL;
}

static DWORD WINAPI OverlayThread(LPVOID param) {
    (void)param;
    MSG pump;
    PeekMessageA(&pump, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    const HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (!LoadPng()) {
        if (SUCCEEDED(co)) CoUninitialize();
        return 0;
    }

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = kClass;
    RegisterClassA(&wc);

    g_overlay = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass,
                                "", WS_POPUP, 0, 0, g_w, g_h, NULL, NULL, wc.hInstance, NULL);
    if (!g_overlay) {
        g_log.Err(&g_log, "CreateWindowEx overlay failed (%lu)", GetLastError());
        FreeBitmap();
        if (SUCCEEDED(co)) CoUninitialize();
        return 0;
    }

    SetTimer(g_overlay, 1, TRACK_MS, NULL);
    SyncOverlay();

    MSG msg;
    while (!InterlockedCompareExchange(&g_stop, 0, 0) && GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_overlay) {
        DestroyWindow(g_overlay);
        g_overlay = NULL;
    }
    UnregisterClassA(kClass, wc.hInstance);
    FreeBitmap();
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

static void EnsureThread(void) {
    if (g_thread) return;
    InterlockedExchange(&g_stop, 0);
    g_thread = CreateThread(NULL, 0, OverlayThread, NULL, 0, &g_tid);
    if (!g_thread) g_log.Err(&g_log, "overlay thread failed (%lu)", GetLastError());
}

static char* __attribute__((thiscall)) Hook_Home(void* this) {
    EnsureThread();
    InterlockedExchange(&g_wantShow, 1);
    char* r = Real_Home(this);
    InterlockedExchange(&g_wantShow, 0);
    return r;
}

void InitCreditsOverlay(void) {
    g_log = CreateLogger("Credits");

    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        g_log.Err(&g_log, "GetModuleHandleW(NULL) failed");
        return;
    }
    BYTE* base = (BYTE*)exe;
    Real_Home = (HomeFn)InlineHook(base + RVA_HOME, (void*)&Hook_Home, HOME_STOLEN, kHomeExpect);
    if (!Real_Home) {
        g_log.Err(&g_log, "home-screen hook failed @ %p", (void*)(base + RVA_HOME));
        return;
    }
    g_log.Suc(&g_log, "home-screen overlay ready @ %p", (void*)(base + RVA_HOME));
}

void ShutdownCreditsOverlay(void) {
    InterlockedExchange(&g_wantShow, 0);
    InterlockedExchange(&g_stop, 1);
    if (g_tid) PostThreadMessageA(g_tid, WM_QUIT, 0, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}
