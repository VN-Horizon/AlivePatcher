#include "CodePage.h"

#include "IatHook.h"
#include "Logger.h"
#include "UiText.h"

#include <commctrl.h>
#include <string.h>

enum { GAME_ACP = 932 }; /* Shift-JIS */

static LRESULT(WINAPI* Real_SendMessageA)(HWND, UINT, WPARAM, LPARAM);
static BOOL(WINAPI* Real_PostMessageA)(HWND, UINT, WPARAM, LPARAM);
static HWND(WINAPI* Real_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
                                          HINSTANCE, LPVOID);
static HMENU(WINAPI* Real_LoadMenuA)(HINSTANCE, LPCSTR);

static void HeapFreePtr(void* p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

static wchar_t* DupAtoW(LPCSTR s) {
    if (!s) return NULL;
    const int n = MultiByteToWideChar(GAME_ACP, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(GAME_ACP, 0, s, -1, w, n);
    return w;
}

static LPCWSTR ResAtoW(LPCSTR s, wchar_t** heap, int translate) {
    *heap = NULL;
    if (!s) return NULL;
    if (IS_INTRESOURCE(s)) return (LPCWSTR)s;
    return *heap = translate ? UiDupTranslateA(s) : DupAtoW(s);
}

static int WINAPI Hook_MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    wchar_t* t = UiDupTranslateA(text);
    wchar_t* c = UiDupTranslateA(caption);
    const int r = MessageBoxW(hwnd, t, c, type);
    HeapFreePtr(t);
    HeapFreePtr(c);
    return r;
}

static int WINAPI Hook_MessageBoxIndirectA(const MSGBOXPARAMSA* a) {
    if (!a) return 0;

    wchar_t *text = NULL, *caption = NULL, *icon = NULL;
    MSGBOXPARAMSW w = {.cbSize = sizeof(w),
                       .hwndOwner = a->hwndOwner,
                       .hInstance = a->hInstance,
                       .lpszText = ResAtoW(a->lpszText, &text, 1),
                       .lpszCaption = ResAtoW(a->lpszCaption, &caption, 1),
                       .dwStyle = a->dwStyle,
                       .lpszIcon = ResAtoW(a->lpszIcon, &icon, 0),
                       .dwContextHelpId = a->dwContextHelpId,
                       .lpfnMsgBoxCallback = a->lpfnMsgBoxCallback,
                       .dwLanguageId = a->dwLanguageId};

    const int r = MessageBoxIndirectW(&w);
    HeapFreePtr(text);
    HeapFreePtr(caption);
    HeapFreePtr(icon);
    return r;
}

static BOOL WINAPI Hook_SetWindowTextA(HWND hwnd, LPCSTR s) {
    wchar_t* w = UiDupTranslateA(s);
    const BOOL ok = SetWindowTextW(hwnd, w);
    HeapFreePtr(w);
    return ok;
}

static HWND WINAPI Hook_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int cx,
                                        int cy, HWND parent, HMENU menu, HINSTANCE hi, LPVOID param) {
    /*
     * Keep the window ANSI: HMC subclasses with SetWindowLongA / DWL_DLGPROC.
     * CreateWindowExW made Unicode windows and config/property sheets crashed.
     * Class names are ASCII; convert only the title via SetWindowTextW.
     */
    HWND hwnd;
    if (!name || IS_INTRESOURCE(name)) {
        hwnd = Real_CreateWindowExA(ex, cls, name, style, x, y, cx, cy, parent, menu, hi, param);
    } else {
        hwnd = Real_CreateWindowExA(ex, cls, NULL, style, x, y, cx, cy, parent, menu, hi, param);
        if (hwnd && name[0]) {
            wchar_t* w = UiDupTranslateA(name);
            SetWindowTextW(hwnd, w);
            HeapFreePtr(w);
        }
    }
    if (hwnd) {
        HMENU m = GetMenu(hwnd);
        if (m) UiApplyMenu(m);
    }
    return hwnd;
}

static HMENU WINAPI Hook_LoadMenuA(HINSTANCE hi, LPCSTR name) {
    HMENU menu = Real_LoadMenuA ? Real_LoadMenuA(hi, name) : LoadMenuA(hi, name);
    if (menu) UiApplyMenu(menu);
    return menu;
}

/* A/W message ids differ for property-sheet title. */
static UINT StringMsgToW(UINT msg) {
    if (msg == PSM_SETTITLEA) return PSM_SETTITLEW;
    if (msg == PSM_SETHEADERTITLEA) return PSM_SETHEADERTITLEW;
    if (msg == PSM_SETHEADERSUBTITLEA) return PSM_SETHEADERSUBTITLEW;
    return msg;
}

static int IsSetStringMsg(UINT msg) {
    return msg == WM_SETTEXT || msg == EM_REPLACESEL || msg == PSM_SETTITLEA || msg == PSM_SETHEADERTITLEA ||
           msg == PSM_SETHEADERSUBTITLEA || msg == LB_ADDSTRING || msg == LB_INSERTSTRING || msg == LB_FINDSTRING ||
           msg == LB_FINDSTRINGEXACT || msg == LB_SELECTSTRING || msg == CB_ADDSTRING || msg == CB_INSERTSTRING ||
           msg == CB_FINDSTRING || msg == CB_FINDSTRINGEXACT || msg == CB_SELECTSTRING;
}

static LRESULT SendTabItemW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const TCITEMA* a = (const TCITEMA*)lp;
    if (!a || !(a->mask & TCIF_TEXT) || !a->pszText) {
        return Real_SendMessageA(hwnd, msg, wp, lp);
    }
    wchar_t* w = UiDupTranslateA(a->pszText);
    TCITEMW t;
    memcpy(&t, a, sizeof(TCITEMA));
    t.pszText = w;
    const UINT wm = (msg == TCM_INSERTITEMA) ? TCM_INSERTITEMW : TCM_SETITEMW;
    const LRESULT r = SendMessageW(hwnd, wm, wp, (LPARAM)&t);
    HeapFreePtr(w);
    return r;
}

static LRESULT WINAPI Hook_SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if ((msg == TCM_INSERTITEMA || msg == TCM_SETITEMA) && lp) return SendTabItemW(hwnd, msg, wp, lp);
    if (!IsSetStringMsg(msg) || !lp) return Real_SendMessageA(hwnd, msg, wp, lp);
    wchar_t* w = UiDupTranslateA((LPCSTR)lp);
    const LRESULT r = SendMessageW(hwnd, StringMsgToW(msg), wp, (LPARAM)w);
    HeapFreePtr(w);
    return r;
}

static BOOL WINAPI Hook_PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!IsSetStringMsg(msg) || !lp) return Real_PostMessageA(hwnd, msg, wp, lp);
    wchar_t* w = UiDupTranslateA((LPCSTR)lp);
    /* PostMessage must keep the buffer until delivered; use SendMessageW for title updates. */
    const BOOL ok = (BOOL)SendMessageW(hwnd, StringMsgToW(msg), wp, (LPARAM)w);
    HeapFreePtr(w);
    return ok;
}

static void OnIatMissing(const char* name, void* ctx) {
    const Logger* logger = (const Logger*)ctx;
    logger->Wrn(logger, "IAT hook missing: %s", name);
}

void InitShiftJisCompat(void) {
    const Logger logger = CreateLogger("CodePage");
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        logger.Err(&logger, "GetModuleHandleW(NULL) failed");
        return;
    }

    InitUiText();

    Real_SendMessageA = SendMessageA;
    Real_PostMessageA = PostMessageA;
    Real_CreateWindowExA = CreateWindowExA;
    Real_LoadMenuA = LoadMenuA;

    /*
     * Do not redirect DialogBox/PropertySheet/CreateWindow to W APIs: HMC swaps
     * DWL_DLGPROC with SetWindowLongA, which crashes on Unicode windows.
     */
    static const IatHookEntry hooks[] = {
        {"USER32.dll", "MessageBoxA", Hook_MessageBoxA, NULL},
        {"USER32.dll", "MessageBoxIndirectA", Hook_MessageBoxIndirectA, NULL},
        {"USER32.dll", "SetWindowTextA", Hook_SetWindowTextA, NULL},
        {"USER32.dll", "CreateWindowExA", Hook_CreateWindowExA, (void**)&Real_CreateWindowExA},
        {"USER32.dll", "LoadMenuA", Hook_LoadMenuA, (void**)&Real_LoadMenuA},
        {"USER32.dll", "SendMessageA", Hook_SendMessageA, (void**)&Real_SendMessageA},
        {"USER32.dll", "PostMessageA", Hook_PostMessageA, (void**)&Real_PostMessageA},
    };

    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnIatMissing, (void*)&logger);
    logger.Suc(&logger, "UTF-8 locale + UI translate: %d hooks (system ACP=%u)", n, GetACP());
}
