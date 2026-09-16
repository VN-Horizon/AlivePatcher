#include "CodePage.h"

#include "IatHook.h"
#include "Logger.h"

#include <commctrl.h>

enum { GAME_ACP = 932 }; /* Shift-JIS */

static LRESULT(WINAPI* Real_SendMessageA)(HWND, UINT, WPARAM, LPARAM);
static BOOL(WINAPI* Real_PostMessageA)(HWND, UINT, WPARAM, LPARAM);

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

static LPCWSTR ResAtoW(LPCSTR s, wchar_t** heap) {
    *heap = NULL;
    if (!s) return NULL;
    if (IS_INTRESOURCE(s)) return (LPCWSTR)s;
    return *heap = DupAtoW(s);
}

static int WINAPI Hook_MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    wchar_t* t = DupAtoW(text);
    wchar_t* c = DupAtoW(caption);
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
                       .lpszText = ResAtoW(a->lpszText, &text),
                       .lpszCaption = ResAtoW(a->lpszCaption, &caption),
                       .dwStyle = a->dwStyle,
                       .lpszIcon = ResAtoW(a->lpszIcon, &icon),
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
    wchar_t* w = DupAtoW(s);
    const BOOL ok = SetWindowTextW(hwnd, w);
    HeapFreePtr(w);
    return ok;
}

static HWND WINAPI Hook_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int cx,
                                        int cy, HWND parent, HMENU menu, HINSTANCE hi, LPVOID param) {
    wchar_t *clsHeap = NULL, *nameHeap = NULL;
    HWND hwnd = CreateWindowExW(ex, ResAtoW(cls, &clsHeap), ResAtoW(name, &nameHeap), style, x, y, cx, cy, parent,
                                menu, hi, param);
    HeapFreePtr(clsHeap);
    HeapFreePtr(nameHeap);
    return hwnd;
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

static LRESULT WINAPI Hook_SendMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!IsSetStringMsg(msg) || !lp) return Real_SendMessageA(hwnd, msg, wp, lp);
    wchar_t* w = DupAtoW((LPCSTR)lp);
    const LRESULT r = SendMessageW(hwnd, StringMsgToW(msg), wp, (LPARAM)w);
    HeapFreePtr(w);
    return r;
}

static BOOL WINAPI Hook_PostMessageA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!IsSetStringMsg(msg) || !lp) return Real_PostMessageA(hwnd, msg, wp, lp);
    wchar_t* w = DupAtoW((LPCSTR)lp);
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

    Real_SendMessageA = SendMessageA;
    Real_PostMessageA = PostMessageA;

    /*
     * Do not redirect DialogBox/PropertySheet to W APIs: HMC swaps DWL_DLGPROC with
     * SetWindowLongA, which crashes on Unicode property-sheet pages (see ConfigDialog).
     */
    static const IatHookEntry hooks[] = {
        {"USER32.dll", "MessageBoxA", Hook_MessageBoxA, NULL},
        {"USER32.dll", "MessageBoxIndirectA", Hook_MessageBoxIndirectA, NULL},
        {"USER32.dll", "SetWindowTextA", Hook_SetWindowTextA, NULL},
        {"USER32.dll", "CreateWindowExA", Hook_CreateWindowExA, NULL},
        {"USER32.dll", "SendMessageA", Hook_SendMessageA, (void**)&Real_SendMessageA},
        {"USER32.dll", "PostMessageA", Hook_PostMessageA, (void**)&Real_PostMessageA},
    };

    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnIatMissing, (void*)&logger);
    logger.Suc(&logger, "UTF-8 locale compat: %d hooks (system ACP=%u)", n, GetACP());
}
