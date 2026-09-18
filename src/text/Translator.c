#include "Translator.h"

#include "Backlog.h"
#include "DialogText.h"
#include "Embed.h"
#include "IatHook.h"
#include "InlineHook.h"
#include "Logger.h"
#include "TransCodec.h"
#include "TransTable.h"

#include <string.h>

enum {
    RVA_PLAY_DIALOG = 0x1E530, /* VA 0x41E530 */
    RVA_PROC_TEXT = 0x1EEB0,   /* VA 0x41EEB0 */
    RVA_SET_NAME = 0x1D190,    /* VA 0x41D190 */
    RVA_SETMBCP = 0x2936D6,    /* VA 0x6936D6 __setmbcp */
    RVA_GETMBCP = 0x293AAE,    /* VA 0x693AAE __getmbcp */
    PLAY_DIALOG_STOLEN = 6,
    PROC_TEXT_STOLEN = 8,
    SET_NAME_STOLEN = 6,
};

typedef int(__attribute__((thiscall)) *PlayDialogFn)(void* this, char* source, int a3, int a4, int a5,
                                                     LONG a6);
typedef unsigned int(__attribute__((thiscall)) *ProcTextFn)(void* this, unsigned char* str, int maxLen);
typedef int(__attribute__((thiscall)) *SetNameFn)(void* this, char* name);
typedef int(__cdecl* SetMbcpFn)(int);
typedef int(__cdecl* GetMbcpFn)(void);

static PlayDialogFn Real_PlayDialog;
static ProcTextFn Real_ProcText;
static SetNameFn Real_SetName;
static HFONT(WINAPI* Real_CreateFontIndirectA)(const LOGFONTA*);
static SetMbcpFn Game_setmbcp;
static GetMbcpFn Game_getmbcp;

static Logger g_log;
static volatile LONG g_inDialog;
static volatile LONG g_inName;
static char g_face[LF_FACESIZE];
static char g_nameGbk[MAX_NAME];

static const BYTE kPlayDialogExpect[PLAY_DIALOG_STOLEN] = {0x81, 0xEC, 0x4C, 0x08, 0x00, 0x00};
static const BYTE kProcTextExpect[PROC_TEXT_STOLEN] = {0x83, 0xEC, 0x1C, 0x53, 0x8B, 0x5C, 0x24, 0x24};
static const BYTE kSetNameExpect[SET_NAME_STOLEN] = {0x83, 0xEC, 0x3C, 0x55, 0x56, 0x57};

static void PushBacklogName(const char* s, UINT cp) {
    wchar_t wide[MAX_NAME];
    if (!s || !s[0] || ToWide(cp, s, wide, MAX_NAME) <= 0) {
        BacklogSetName(L"");
        return;
    }
    BacklogSetName(wide);
}

static void PushBacklogLine(const char* source) {
    char gbk[MAX_LINE];
    wchar_t wide[MAX_WIDE];
    if (!source || !source[0]) return;
    if (!TranslateSjisToGbk(source, gbk, (int)sizeof(gbk), 0)) return;
    if (ToWide(CN_ACP, gbk, wide, MAX_WIDE) <= 0) return;
    BacklogAdd(wide);
}

static void PickFace(void) {
    static const char* kFaces[] = {"Microsoft YaHei UI",
                                   "Microsoft YaHei",
                                   "SimSun",
                                   "NSimSun",
                                   "SimHei",
                                   "KaiTi",
                                   "FangSong",
                                   "PMingLiU",
                                   "MingLiU"};
    g_face[0] = 0;
    for (int i = 0; i < (int)(sizeof(kFaces) / sizeof(kFaces[0])); ++i) {
        LOGFONTA lf;
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = 24;
        lf.lfCharSet = GB2312_CHARSET;
        strncpy(lf.lfFaceName, kFaces[i], LF_FACESIZE - 1);
        HFONT h = CreateFontIndirectA(&lf);
        if (!h) continue;
        HDC dc = CreateCompatibleDC(NULL);
        if (!dc) {
            DeleteObject(h);
            continue;
        }
        HGDIOBJ old = SelectObject(dc, h);
        TEXTMETRICA tm;
        ZeroMemory(&tm, sizeof(tm));
        const BOOL ok = GetTextMetricsA(dc, &tm);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(h);
        if (ok && tm.tmHeight > 0) {
            strncpy(g_face, kFaces[i], LF_FACESIZE - 1);
            g_face[LF_FACESIZE - 1] = 0;
            return;
        }
    }
}

static HFONT WINAPI Hook_CreateFontIndirectA(const LOGFONTA* lf) {
    if (!g_inDialog || !lf || !Real_CreateFontIndirectA) {
        return Real_CreateFontIndirectA ? Real_CreateFontIndirectA(lf) : NULL;
    }
    LOGFONTA copy = *lf;
    copy.lfCharSet = GB2312_CHARSET;
    /*
     * Engine LOGFONT is MS Gothic: lfWidth = height/2 (half-width cell) and
     * positive lfHeight (cell includes leading). YaHei then looks squashed.
     * Zero width keeps the face's aspect; negative height is em size.
     */
    // TODO: FIX FLAT TEXT in name box (need position recalc) 对不起只能暂时压你一下了.
    if (!g_inName) {
        copy.lfWidth = 0;
        if (copy.lfHeight > 0) copy.lfHeight = -copy.lfHeight;
    }
    if (g_face[0]) {
        ZeroMemory(copy.lfFaceName, sizeof(copy.lfFaceName));
        strncpy(copy.lfFaceName, g_face, LF_FACESIZE - 1);
    }
    return Real_CreateFontIndirectA(&copy);
}

static unsigned int __attribute__((thiscall)) Hook_ProcText(void* this, unsigned char* str, int maxLen) {
    if (str && maxLen > 1) {
        char tmp[MAX_LINE];
        int n = 0;
        while (n < maxLen - 1 && n < MAX_LINE - 1 && str[n]) ++n;
        if (n > 0) {
            memcpy(tmp, str, (size_t)n);
            tmp[n] = 0;
            if (TranslateSjisToGbk(tmp, (char*)str, maxLen, 0) && Game_setmbcp) Game_setmbcp(CN_ACP);
        }
    }
    return Real_ProcText(this, str, maxLen);
}

static int __attribute__((thiscall)) Hook_SetName(void* this, char* name) {
    char* src = name;
    if (name && name[0] && TranslateNameSjisToGbk(name, g_nameGbk, (int)sizeof(g_nameGbk))) {
        src = g_nameGbk;
        PushBacklogName(g_nameGbk, CN_ACP);
    } else {
        PushBacklogName(name, GAME_ACP);
    }
    InterlockedExchange(&g_inName, 1);
    const int r = Real_SetName(this, src);
    InterlockedExchange(&g_inName, 0);
    return r;
}

static int __attribute__((thiscall)) Hook_PlayDialog(void* this, char* source, int a3, int a4, int a5,
                                                     LONG a6) {
    char buf[MAX_LINE];
    const int oldCp = Game_getmbcp ? Game_getmbcp() : 0;
    InterlockedExchange(&g_inDialog, 1);

    char* src = source;
    if (a5 != 0 && source && TranslateSjisToGbk(source, buf, (int)sizeof(buf), 1)) {
        src = buf;
        if (Game_setmbcp) Game_setmbcp(CN_ACP);
    }
    if (source) PushBacklogLine(source);

    const int r = Real_PlayDialog(this, src, a3, a4, a5, a6);

    if (Game_setmbcp) Game_setmbcp(oldCp);
    InterlockedExchange(&g_inDialog, 0);
    return r;
}

static void OnIatMissing(const char* name, void* ctx) {
    const Logger* logger = (const Logger*)ctx;
    logger->Wrn(logger, "IAT hook missing: %s", name);
}

void InitTranslator(void) {
    g_log = CreateLogger("Translator");
    InitBacklog();

    const void* bin = NULL;
    DWORD binSize = 0;
    if (!EmbedGet("TRANSLATION", &bin, &binSize) || !bin || !binSize) {
        g_log.Err(&g_log, "translation.bin resource missing");
        return;
    }
    if (!TransTableLoad((const BYTE*)bin, binSize)) {
        g_log.Err(&g_log, "failed to parse translation.bin (%lu bytes)", binSize);
        return;
    }
    g_log.Suc(&g_log, "loaded %u dialog translations", TransTableCount());

    PickFace();
    if (g_face[0])
        g_log.Inf(&g_log, "dialog font: %s (GBK)", g_face);
    else
        g_log.Wrn(&g_log, "no CJK font found; Chinese may not render");

    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        g_log.Err(&g_log, "GetModuleHandleW(NULL) failed");
        return;
    }
    BYTE* base = (BYTE*)exe;

    Game_setmbcp = (SetMbcpFn)(base + RVA_SETMBCP);
    Game_getmbcp = (GetMbcpFn)(base + RVA_GETMBCP);
    if (base[RVA_SETMBCP] != 0x55 || base[RVA_SETMBCP + 1] != 0x8B || base[RVA_SETMBCP + 2] != 0xEC) {
        g_log.Err(&g_log, "_setmbcp prologue mismatch");
        Game_setmbcp = NULL;
    }
    if (base[RVA_GETMBCP] != 0xA1) {
        g_log.Err(&g_log, "_getmbcp prologue mismatch");
        Game_getmbcp = NULL;
    }

    Real_PlayDialog =
        (PlayDialogFn)InlineHook(base + RVA_PLAY_DIALOG, (void*)&Hook_PlayDialog, PLAY_DIALOG_STOLEN,
                                 kPlayDialogExpect);
    if (!Real_PlayDialog) {
        g_log.Err(&g_log, "PlayDialog hook failed @ %p", (void*)(base + RVA_PLAY_DIALOG));
        return;
    }
    Real_ProcText = (ProcTextFn)InlineHook(base + RVA_PROC_TEXT, (void*)&Hook_ProcText, PROC_TEXT_STOLEN,
                                           kProcTextExpect);
    if (!Real_ProcText) {
        g_log.Err(&g_log, "ProcText hook failed @ %p", (void*)(base + RVA_PROC_TEXT));
        return;
    }
    Real_SetName =
        (SetNameFn)InlineHook(base + RVA_SET_NAME, (void*)&Hook_SetName, SET_NAME_STOLEN, kSetNameExpect);
    if (!Real_SetName) {
        g_log.Err(&g_log, "SetName hook failed @ %p", (void*)(base + RVA_SET_NAME));
        return;
    }

    Real_CreateFontIndirectA = CreateFontIndirectA;
    static const IatHookEntry hooks[] = {
        {"GDI32.dll", "CreateFontIndirectA", Hook_CreateFontIndirectA, (void**)&Real_CreateFontIndirectA},
    };
    IatHookAll(exe, hooks, sizeof(hooks) / sizeof(hooks[0]), OnIatMissing, (void*)&g_log);
    g_log.Suc(&g_log, "PlayDialog hooked @ %p", (void*)(base + RVA_PLAY_DIALOG));
}
