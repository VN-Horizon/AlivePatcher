#include "ConfigDialog.h"

#include "IatHook.h"
#include "Logger.h"

#include <commctrl.h>
#include <string.h>

/* Sites: jz+5; add eax,4; jmp+2; xor eax,eax; mov <reg>,[eax+4] */
static const DWORD kNullHwndSites[] = {
    0x0707C, /* VA 0x40707C */
    0x1AF00, /* VA 0x41AF00 save/load */
    0x1BC8C, /* VA 0x41BC8C config */
};

static HWND(WINAPI* Real_GetParent)(HWND);
static Logger g_log;

/*
 * HMC resolves CWindow with GetPropA(GetParent(hwnd), "HMC::CWindow").
 * Config/save-load then do *(found+8) without a NULL check. On NT, GetParent
 * of an overlapped frame is NULL (unlike typical Win9x WS_POPUP fullscreen
 * hierarchies), so the read of address 4 crashes.
 */
static HWND WINAPI Hook_GetParent(HWND hWnd) {
    HWND p = Real_GetParent ? Real_GetParent(hWnd) : NULL;
    if (p || !hWnd) return p;

    p = GetWindow(hWnd, GW_OWNER);
    if (p) return p;

    if (GetPropA(hWnd, "HMC::CWindow")) return hWnd;
    return NULL;
}

static BYTE XorRegOpcode(BYTE modrm) {
    /* mov r32, [eax+4] => ModRM 0b01_reg_000; xor r32,r32 => 0b11_reg_reg */
    const BYTE reg = (BYTE)((modrm >> 3) & 7);
    return (BYTE)(0xC0 | (reg << 3) | reg);
}

static int PatchNullHwndSite(BYTE* p) {
    /* 74 ?? 83 C0 04 EB 02 33 C0 8B xx 04 */
    if (p[0] != 0x74 || p[2] != 0x83 || p[3] != 0xC0 || p[4] != 0x04 || p[5] != 0xEB || p[7] != 0x33 ||
        p[8] != 0xC0 || p[9] != 0x8B || p[11] != 0x04) {
        return 0;
    }

    const BYTE modrm = p[10];
    const BYTE xorRm = XorRegOpcode(modrm);

    DWORD old = 0;
    if (!VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old)) return 0;

    /*
     * 74 08        jz xor_reg
     * 83 C0 04     add eax, 4
     * 8B xx 04     mov reg, [eax+4]
     * EB 02        jmp continue
     * 33 xx        xor reg, reg
     * 90           nop
     */
    p[0] = 0x74;
    p[1] = 0x08;
    p[2] = 0x83;
    p[3] = 0xC0;
    p[4] = 0x04;
    p[5] = 0x8B;
    p[6] = modrm;
    p[7] = 0x04;
    p[8] = 0xEB;
    p[9] = 0x02;
    p[10] = 0x33;
    p[11] = xorRm;
    p[12] = 0x90;

    VirtualProtect(p, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 13);
    return 1;
}

static void PatchNullHwndSites(void) {
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) return;

    int n = 0;
    for (size_t i = 0; i < sizeof(kNullHwndSites) / sizeof(kNullHwndSites[0]); ++i) {
        BYTE* p = (BYTE*)exe + kNullHwndSites[i];
        if (PatchNullHwndSite(p)) {
            g_log.Suc(&g_log, "null-hwnd guard @ %p", (void*)p);
            ++n;
        } else {
            g_log.Err(&g_log, "null-hwnd site mismatch @ %p (%.2X %.2X %.2X)", (void*)p, p[0], p[1], p[2]);
        }
    }
    g_log.Inf(&g_log, "patched %d/%d null-hwnd sites", n,
              (int)(sizeof(kNullHwndSites) / sizeof(kNullHwndSites[0])));
}

static void OnIatMissing(const char* name, void* ctx) {
    const Logger* logger = (const Logger*)ctx;
    logger->Wrn(logger, "IAT hook missing: %s", name);
}

static void InitCommonControlsForSheets(void) {
    INITCOMMONCONTROLSEX icc;
    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS |
                ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&icc)) InitCommonControls();
}

void InitConfigDialog(void) {
    g_log = CreateLogger("ConfigDialog");

    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        g_log.Err(&g_log, "GetModuleHandleW(NULL) failed");
        return;
    }

    InitCommonControlsForSheets();
    PatchNullHwndSites();

    Real_GetParent = GetParent;
    static const IatHookEntry hooks[] = {
        {"USER32.dll", "GetParent", Hook_GetParent, (void**)&Real_GetParent},
    };
    const int n = IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnIatMissing, (void*)&g_log);
    g_log.Suc(&g_log, "GetParent Win9x-compat ready (hooks=%d)", n);
}
