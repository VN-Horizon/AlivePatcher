#include "ConfigDialog.h"

#include "IatHook.h"
#include "Logger.h"

#include <string.h>

#ifndef PROCESS_CALLBACK_FILTER_ENABLED
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1
#endif

#ifndef BS_TYPEMASK
#define BS_TYPEMASK 0x0000000FL
#endif

enum {
    RVA_CDDA_INIT = 0xA341,  /* VA 0x40A341: CD-DA drive enum on the sound page */
    RVA_CDDA_AFTER = 0xA7C7, /* VA 0x40A7C7: next settings block (id 1046) */
    RVA_ENABLE_CTRL = 0x6000, /* VA 0x406000: HMC::CControl::EnableWindow */
    ID_SAVE_SLOT1 = 0x402,    /* 1026: also BGM enable on the sound page */
    ID_VOL_BGM = 1008,
    ID_VOL_SE = 1010,
    ID_VOL_VOICE = 1011,
    ID_UD_BGM = 1014,
    ID_UD_SE = 1015,
    ID_UD_VOICE = 1016,
    ID_CDDA_COMBO = 0x411, /* 1041 */
    ID_CDDA_CHK2 = 0x412,  /* 1042 */
    ID_CDDA_CHK1 = 0x413,  /* 1043 */
    ID_CDDA_PATH2_EDIT = 0x40F, /* 1039 */
    ID_CDDA_PATH1_EDIT = 0x410, /* 1040 */
    ID_CDDA_PATH2_CHK = 0x414,  /* 1044 */
    ID_CDDA_PATH0_CHK = 0x415,  /* 1045 */
    ID_CDDA_PATH0_TOP = 0x416,  /* 1046 */
    ID_CDDA_PATH0_EDIT = 0x417, /* 1047 */
};

typedef int(WINAPI* PropertySheetAFn)(void*);

static PropertySheetAFn Real_PropertySheetA;
static volatile LONG g_inPropertySheet;
static Logger g_log;

/*
 * Win7+ turns an AV inside a dialog proc into c000041d. The engine probes
 * PSD headers by reading unmapped pointers (sub_68C0D0 at RVA 0x28C0DF).
 * Title-screen loads must still take that AV so SEH can try ARC/PSD fallback.
 */
static void AllowCallbackExceptions(void) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    typedef BOOL(WINAPI* GetPol)(LPDWORD);
    typedef BOOL(WINAPI* SetPol)(DWORD);
    GetPol getp = (GetPol)GetProcAddress(k32, "GetProcessUserModeExceptionPolicy");
    SetPol setp = (SetPol)GetProcAddress(k32, "SetProcessUserModeExceptionPolicy");
    DWORD flags = 0;
    if (getp && setp && getp(&flags)) setp(flags & ~PROCESS_CALLBACK_FILTER_ENABLED);
}

static int ClientCenterIn(HWND child, const RECT* box) {
    RECT r;
    if (!GetWindowRect(child, &r)) return 0;
    const POINT c = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    return c.x >= box->left && c.x < box->right && c.y >= box->top && c.y < box->bottom;
}

static int DlgIdCenterIn(HWND page, int id, const RECT* box) {
    HWND c = GetDlgItem(page, id);
    return c && ClientCenterIn(c, box);
}

static int IsCdDaCtrlId(int id) {
    switch (id) {
        case ID_CDDA_COMBO:
        case ID_CDDA_CHK1:
        case ID_CDDA_CHK2:
        case ID_CDDA_PATH2_EDIT:
        case ID_CDDA_PATH1_EDIT:
        case ID_CDDA_PATH2_CHK:
        case ID_CDDA_PATH0_CHK:
        case ID_CDDA_PATH0_TOP:
        case ID_CDDA_PATH0_EDIT:
            return 1;
        default:
            return 0;
    }
}

static int GroupHasCdDa(HWND page, const RECT* g) {
    static const int ids[] = {ID_CDDA_COMBO,      ID_CDDA_CHK1,      ID_CDDA_CHK2,      ID_CDDA_PATH0_TOP,
                              ID_CDDA_PATH0_EDIT, ID_CDDA_PATH0_CHK, ID_CDDA_PATH1_EDIT, ID_CDDA_PATH2_CHK,
                              ID_CDDA_PATH2_EDIT};
    for (int i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); ++i) {
        if (DlgIdCenterIn(page, ids[i], g)) return 1;
    }
    return 0;
}

static void HideCdDaZone(HWND page) {
    HWND child = GetWindow(page, GW_CHILD);
    while (child) {
        if (IsCdDaCtrlId(GetDlgCtrlID(child))) ShowWindow(child, SW_HIDE);
        child = GetWindow(child, GW_HWNDNEXT);
    }

    /* Hide CD-DA-only groups and leftover labels inside them. Keep BGM/volume. */
    child = GetWindow(page, GW_CHILD);
    while (child) {
        const LONG style = GetWindowLongA(child, GWL_STYLE);
        RECT g;
        if ((style & BS_TYPEMASK) == BS_GROUPBOX && GetWindowRect(child, &g) && GroupHasCdDa(page, &g) &&
            !DlgIdCenterIn(page, ID_VOL_BGM, &g) && !DlgIdCenterIn(page, ID_VOL_SE, &g) &&
            !DlgIdCenterIn(page, ID_SAVE_SLOT1, &g)) {
            ShowWindow(child, SW_HIDE);
            HWND inner = GetWindow(page, GW_CHILD);
            while (inner) {
                if (inner != child && ClientCenterIn(inner, &g) && GetDlgCtrlID(inner) != ID_VOL_BGM &&
                    GetDlgCtrlID(inner) != ID_VOL_SE && GetDlgCtrlID(inner) != ID_VOL_VOICE) {
                    ShowWindow(inner, SW_HIDE);
                }
                inner = GetWindow(inner, GW_HWNDNEXT);
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static LRESULT CALLBACK OnSheetWndRet(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp) {
        const CWPRETSTRUCT* msg = (const CWPRETSTRUCT*)lp;
        if (msg->message == WM_INITDIALOG && msg->hwnd &&
            (GetDlgItem(msg->hwnd, ID_CDDA_COMBO) || GetDlgItem(msg->hwnd, ID_CDDA_PATH0_TOP))) {
            HideCdDaZone(msg->hwnd);
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static int WINAPI Hook_PropertySheetA(void* hdr) {
    InterlockedIncrement(&g_inPropertySheet);
    const HHOOK hide = SetWindowsHookExW(WH_CALLWNDPROCRET, OnSheetWndRet, NULL, GetCurrentThreadId());
    const int r = Real_PropertySheetA ? Real_PropertySheetA(hdr) : 0;
    if (hide) UnhookWindowsHookEx(hide);
    InterlockedDecrement(&g_inPropertySheet);
    return r;
}

/*
 * CConfigSound turns BGM/SE/voice sliders off when it thinks CD-DA/mixer
 * inherit is active. Replacement BGM is XAudio2, so keep those controls on.
 * HMC::CControl: vtable+0, HWND at +4 (sub_406000).
 */
static BOOL __attribute__((thiscall)) Hook_EnableCtrl(HWND* ctl, BOOL enable) {
    HWND h = ctl ? ctl[1] : NULL;
    if (h) {
        switch (GetDlgCtrlID(h)) {
            case ID_VOL_BGM:
            case ID_VOL_SE:
            case ID_VOL_VOICE:
            case ID_UD_BGM:
            case ID_UD_SE:
            case ID_UD_VOICE:
                enable = TRUE;
                break;
            default:
                break;
        }
    }
    return EnableWindow(h, enable);
}

static int PatchEnableCtrl(BYTE* exe) {
    BYTE* p = exe + RVA_ENABLE_CTRL;
    if (p[0] != 0x8B || p[1] != 0x44 || p[2] != 0x24 || p[3] != 0x04) {
        g_log.Err(&g_log, "EnableCtrl mismatch at %p (%.2X %.2X %.2X %.2X)", (void*)p, p[0], p[1], p[2], p[3]);
        return 0;
    }
    DWORD old = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    const UINT_PTR dest = (UINT_PTR)&Hook_EnableCtrl;
    const INT32 rel = (INT32)(dest - ((UINT_PTR)p + 5));
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    g_log.Suc(&g_log, "CControl::EnableWindow hooked @ %p", (void*)p);
    return 1;
}

static LONG CALLBACK OnParserProbeAv(PEXCEPTION_POINTERS ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    if (ep->ExceptionRecord->NumberParameters >= 1 && ep->ExceptionRecord->ExceptionInformation[0] != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    BYTE* exe = (BYTE*)GetModuleHandleW(NULL);
    if (!exe) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD rva = ep->ContextRecord->Eip - (DWORD)(uintptr_t)exe;
    CONTEXT* ctx = ep->ContextRecord;

    if (!g_inPropertySheet) return EXCEPTION_CONTINUE_SEARCH;

    /* Config PropertySheet: treat the probe as "not a PSD" (fail epilogue). */
    if (rva == 0x28C0DF) {
        ctx->Eip = (DWORD)(uintptr_t)(exe + 0x28C2CA);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    /* sub_407F80: get-child(0/1) is NULL when a trackbar notifies during init. */
    if (ctx->Edi == 0 && rva >= 0x8029 && rva <= 0x807E) {
        ctx->Eip = (DWORD)(uintptr_t)(exe + (rva < 0x8058 ? 0x8058u : 0x8081u));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Skip CD-DA drive enum / MCI on empty trays (jmp over ids 1043, 1042, 1041). */
static int PatchSkipCdDaInit(BYTE* exe) {
    BYTE* p = exe + RVA_CDDA_INIT;
    if (p[0] != 0x8B || p[1] != 0x46 || p[2] != 0x08) {
        g_log.Err(&g_log, "CD-DA init mismatch at %p (%.2X %.2X %.2X)", (void*)p, p[0], p[1], p[2]);
        return 0;
    }

    DWORD old = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
        g_log.Err(&g_log, "VirtualProtect CD-DA skip failed (%lu)", GetLastError());
        return 0;
    }
    const INT32 rel = (INT32)(RVA_CDDA_AFTER - (RVA_CDDA_INIT + 5));
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    g_log.Suc(&g_log, "CD-DA init skipped @ %p", (void*)p);
    return 1;
}

static void OnIatMissing(const char* name, void* ctx) {
    const Logger* logger = ctx;
    logger->Wrn(logger, "IAT hook missing: %s", name);
}

void InitConfigDialog(void) {
    g_log = CreateLogger("ConfigDialog");
    AllowCallbackExceptions();
    AddVectoredExceptionHandler(1, OnParserProbeAv);

    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        g_log.Err(&g_log, "GetModuleHandleW(NULL) failed");
        return;
    }

    PatchSkipCdDaInit((BYTE*)exe);
    PatchEnableCtrl((BYTE*)exe);

    Real_PropertySheetA = (PropertySheetAFn)GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "PropertySheetA");
    static const IatHookEntry hooks[] = {
        {"COMCTL32.dll", "PropertySheetA", Hook_PropertySheetA, (void**)&Real_PropertySheetA},
    };
    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnIatMissing, (void*)&g_log);
    g_log.Suc(&g_log, "PropertySheetA probe guard ready (hooks=%d)", n);
}
