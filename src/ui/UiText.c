#include "UiText.h"

#include "Logger.h"
#include "TransCodec.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0501
#endif
#include <commctrl.h>
#include <prsht.h>
#include <stdlib.h>
#include <string.h>

typedef struct UiPair {
    const wchar_t* from;
    const wchar_t* to;
} UiPair;

static UiPair* g_sorted;
static size_t g_npair;
static HFONT g_font;
static Logger g_log;
static wchar_t g_tmpl[512];

static const UiPair kPairs[] = {
    {L"%sのバージョン情報", L"%s的版本信息"},
    {L"Alive renewal 起動メニュー", L"Alive renewal 启动菜单"},
    {L"Alive renewalのセットアップ(&S)", L"Alive renewal 安装(&S)"},
    {L"Alive renewalのバージョン情報", L"Alive renewal的版本信息"},
    {L"Alive renewalの終了(&X)", L"退出(&X)"},
    {L"Alive renewalの設定", L"Alive renewal的设置"},
    {L"Alive renewalを起動する(&O)", L"启动 Alive renewal(&O)"},
    {L"CODECが読み込まれていません", L"未加载编解码器"},
    {L"CODECのバージョン情報", L"编解码器版本信息"},
    {L"CODECのバージョン情報(&C)", L"编解码器版本信息(&C)"},
    {L"DirectXのセットアップ(&M)", L"DirectX 安装(&M)"},
    {L"SE 音量", L"SE 音量"},
    {L"VOICE 音量", L"VOICE 音量"},
    {L"BGM 音量", L"BGM 音量"},
    {L"CD-DA設定", L"CD-DA设置"},
    {L"CDDB用のDISCIDを検出する", L"检测CDDB用DISCID"},
    {L"UPCコードを検出する", L"检测UPC码"},
    {L"ｱﾌﾟﾘｹｰｼｮﾝの終了(&X)", L"退出(&X)"},
    {L"ｺﾋﾟｰ(&C)\tCtrl+C", L"复制(&C)\tCtrl+C"},
    {L"ｷｬﾝｾﾙ", L"取消"},
    {L"ﾊﾞｰｼﾞｮﾝ情報(Alive renewal) (&A)...", L"版本信息(Alive renewal)(&A)..."},
    {L"ﾌｧｲﾙ(&F)", L"文件(&F)"},
    {L"ﾍﾙﾌﾟ(&H)", L"帮助(&H)"},
    {L"はい", L"是"},
    {L"いいえ", L"否"},
    {L"なし", L"无"},
    {L"アンチエイリアス", L"抗锯齿"},
    {L"ウィンドウを隠す(&H)", L"显/隐对话框(&H)"},
    {L"オートセーブを有効にする", L"启用自动存档"},
    {L"オートセーブ時のコメント(半角255文字相当まで)", L"自动存档时的注释 (最多255个半角字符)"},
    {L"キャンセル", L"取消"},
    {L"コメント入力(半角255文字相当まで)", L"注释输入(最多255个半角字符)"},
    {L"シナリオウォーカー", L"剧本查看器"},
    {L"システムのボリューム設定を使う", L"使用系统的音量设置"},
    {L"ジャンプ(&J)", L"跳转(&J)"},
    {L"スクリーンショット", L"截图"},
    {L"セーブ(&S)", L"保存(&S)"},
    {L"セーブ・ロード", L"存档・读档"},
    {L"セーブ・ロード設定", L"存档・读档设置"},
    {L"セーブダイアログの動作", L"存档对话框行为"},
    {L"ダイアログを開いた時、最後にセーブしたデータを自動的に選択する",
     L"打开对话框时，自动选择最后存档的数据"},
    {L"ダブルクリックで選択してロードした時、ダイアログを自動的に閉じる",
     L"双击选择读档时，自动关闭对话框"},
    {L"データセーブ", L"保存数据"},
    {L"データロード", L"读取数据"},
    {L"データロード時に確認メッセージを表示する", L"读档时显示确认信息"},
    {L"デバッグログ", L"调试日志"},
    {L"ドロップシャドウ", L"投影"},
    {L"ただしオートセーブでセーブされたデータは自動的に選択しない", L"但是不自动选择自动存档的数据"},
    {L"ファイル(&F)", L"文件(&F)"},
    {L"フェードを有効にする", L"启用淡入淡出"},
    {L"フォント選択", L"字体选择"},
    {L"フォント設定", L"字体设置"},
    {L"フルスクリーンモード(&F)", L"全屏模式(&F)"},
    {L"ヘルプ(&H)", L"帮助(&H)"},
    {L"ボリュームラベルを検出する", L"检测卷标"},
    {L"メッセージ自動送り(&K)", L"文本自动播放(&K)"},
    {L"メッセージ自動送り速度", L"文本自动播放速度"},
    {L"メッセージ速度", L"文本速度"},
    {L"ロード(&L)", L"加载(&L)"},
    {L"ロードしますか？", L"要读取存档吗？"},
    {L"ロードダイアログの動作", L"读档对话框行为"},
    {L"ログファイル(&F)", L"日志文件(&F)"},
    {L"ログ出力(&O)", L"输出日志(&O)"},
    {L"上書きしますか？", L"要覆盖吗？"},
    {L"上書きセーブ時に確認メッセージを表示する", L"覆盖存档时显示确认信息"},
    {L"上書き保存(&S)\tCtrl+S", L"保存(&S)\tCtrl+S"},
    {L"主人公の名前", L"主人公的姓名"},
    {L"優先するCD-ROMドライブ", L"优先使用的CD-ROM驱动器"},
    {L"元に戻す(&U)\tCtrl+Z", L"撤销(&U)\tCtrl+Z"},
    {L"切り取り(&T)\tCtrl+X", L"剪切(&T)\tCtrl+X"},
    {L"初期化", L"初始化"},
    {L"名前", L"名"},
    {L"名前を付けて保存(&A)...", L"另存为(&A)..."},
    {L"名字", L"姓"},
    {L"基本デバッグ", L"基础调试"},
    {L"基本設定", L"基础设置"},
    {L"遅い", L"慢"},
    {L"即座に反映する", L"立即生效"},
    {L"項目をダブルクリックしてセーブした時、ダイアログを自動的に閉じる",
     L"双击项目存档时，自动关闭对话框"},
    {L"画面効果速度", L"画面效果速度"},
    {L"確認", L"确认"},
    {L"終了", L"结束"},
    {L"終了(&C)", L"结束(&C)"},
    {L"編集(&E)", L"编辑(&E)"},
    {L"表示サンプル", L"显示示例"},
    {L"設定(&C)", L"设置(&C)"},
    {L"詳細設定", L"详细设置"},
    {L"貼り付け(&P)\tCtrl+V", L"粘贴(&P)\tCtrl+V"},
    {L"起動時にBGMディスクが挿入されていることを確認する", L"启动时确认已插入BGM光盘"},
    {L"速い", L"快"},
    {L"適用(&A)", L"应用(&A)"},
    {L"適用(A)", L"应用(A)"},
    {L"音量設定", L"音量设置"},
    {L"音声設定", L"声音设置"},
    {L"サウンド設定", L"声音设置"},
    {L"最大", L"最大"},
    {L"最新のﾌｧｲﾙ", L"最近的文件"},
    {L"最初から始める(&N)", L"主界面(&N)"},
    {L"最初にお読みください(&R)", L"请先阅读(&R)"},
    {L"新規", L"新建"},
    {L"新規(&N) ", L"新建(&N) "},
    {L"新規作成(&N)\tCtrl+N", L"新建(&N)\tCtrl+N"},
    {L"番のデータをオートセーブの対象にする", L"号数据作为自动存档对象"},
    {L"開く(&O)...\tCtrl+O", L"打开(&O)...\tCtrl+O"},
    {L"バージョン情報", L"版本信息"},
    {L"バージョン情報(&A)...", L"版本信息(&A)..."},
};

static int PairCmp(const void* a, const void* b) {
    const UiPair* ea = (const UiPair*)a;
    const UiPair* eb = (const UiPair*)b;
    return wcscmp(ea->from ? ea->from : L"", eb->from ? eb->from : L"");
}

static void EnsureTable(void) {
    if (g_sorted) return;
    g_npair = sizeof(kPairs) / sizeof(kPairs[0]);
    g_sorted = (UiPair*)HeapAlloc(GetProcessHeap(), 0, g_npair * sizeof(UiPair));
    if (!g_sorted) {
        g_npair = 0;
        return;
    }
    memcpy(g_sorted, kPairs, g_npair * sizeof(UiPair));
    qsort(g_sorted, g_npair, sizeof(UiPair), PairCmp);
}

static const wchar_t* LookupExact(const wchar_t* s) {
    if (!s || !s[0] || !g_sorted || !g_npair) return NULL;
    UiPair key = {.from = s, .to = NULL};
    const UiPair* hit = (const UiPair*)bsearch(&key, g_sorted, g_npair, sizeof(UiPair), PairCmp);
    return hit ? hit->to : NULL;
}

static const wchar_t kSheetTitle[] = L"Alive renewal的设置";

static const wchar_t* UiLookup(const wchar_t* s) {
    const wchar_t* hit = LookupExact(s);
    if (hit) return hit;
    if (!s || !s[0]) return NULL;

    wchar_t cut[512];
    wcsncpy(cut, s, 511);
    cut[511] = 0;
    wchar_t* suf = wcsstr(cut, L" Properties");
    if (!suf) suf = wcsstr(cut, L" properties");
    if (!suf) suf = wcsstr(cut, L"のプロパティ");
    if (suf) {
        *suf = 0;
        while (suf > cut && suf[-1] == L' ') *--suf = 0;
        hit = LookupExact(cut);
        if (hit) return hit;
        /* PSH_PROPTITLE only: "Alive renewal Properties", not the game frame title. */
        if (wcsncmp(cut, L"Alive renewal", 13) == 0) return kSheetTitle;
    }

    static const wchar_t kVer[] = L"のバージョン情報";
    const size_t n = wcslen(s);
    const size_t vn = wcslen(kVer);
    if (n > vn && wcscmp(s + (n - vn), kVer) == 0 && n - vn + 8 < 500) {
        memcpy(g_tmpl, s, (n - vn) * sizeof(wchar_t));
        wcscpy(g_tmpl + (n - vn), L"的版本信息");
        return g_tmpl;
    }

    static const wchar_t kBgm[] = L"』のBGMディスクをCD-ROMドライブに挿入してください";
    if (s[0] == 0x300E) {
        const wchar_t* suf = wcsstr(s, kBgm);
        if (suf && wcscmp(suf, kBgm) == 0) {
            const size_t inner = (size_t)(suf - s);
            if (inner + 24 < 500) {
                g_tmpl[0] = L'请';
                g_tmpl[1] = L'将';
                memcpy(g_tmpl + 2, s, inner * sizeof(wchar_t));
                wcscpy(g_tmpl + 2 + inner, L"』的BGM光盘插入CD-ROM驱动器");
                return g_tmpl;
            }
        }
    }
    return NULL;
}

static wchar_t* DupW(const wchar_t* s) {
    if (!s) return NULL;
    const size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t* d = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, n);
    if (d) memcpy(d, s, n);
    return d;
}

static UINT UiAcp(void) {
    const UINT acp = GetACP();
    if (acp == 65001) return CP_UTF8;
    if (acp == 936 || acp == 950) return acp;
    return CN_ACP;
}

static int ToUiAcp(const wchar_t* w, char* out, int max) {
    return ToNarrow(UiAcp(), w, out, max);
}

void* UiPatchPropertySheet(void* hdr) {
    if (!hdr) return hdr;
    EnsureTable();

    static PROPSHEETHEADERA copy;
    static char cap[256];

    const DWORD cb = *(const DWORD*)hdr;
    if (cb < 28 || cb > sizeof(copy)) return hdr;
    ZeroMemory(&copy, sizeof(copy));
    memcpy(&copy, hdr, cb);

    copy.dwFlags &= ~PSH_PROPTITLE;
    ToUiAcp(kSheetTitle, cap, (int)sizeof(cap));
    copy.pszCaption = cap;
    return &copy;
}

wchar_t* UiDupTranslateA(LPCSTR s) {
    if (!s) return NULL;
    EnsureTable();
    wchar_t wide[MAX_WIDE];
    if (ToWide(GAME_ACP, s, wide, MAX_WIDE) <= 0) {
        /* empty or convert fail: still keep empty string */
        return DupW(L"");
    }
    const wchar_t* tr = UiLookup(wide);
    return DupW(tr ? tr : wide);
}

static HFONT MakeFace(const wchar_t* face, int px) {
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = GB2312_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    return CreateFontIndirectW(&lf);
}

static void PickFont(void) {
    static const wchar_t* kFaces[] = {L"Microsoft YaHei UI", L"Microsoft YaHei", L"SimSun", L"NSimSun",
                                      L"PMingLiU"};
    for (int i = 0; i < (int)(sizeof(kFaces) / sizeof(kFaces[0])); ++i) {
        HFONT h = MakeFace(kFaces[i], 12);
        if (!h) continue;
        HDC dc = CreateCompatibleDC(NULL);
        if (!dc) {
            DeleteObject(h);
            continue;
        }
        HGDIOBJ old = SelectObject(dc, h);
        TEXTMETRICW tm;
        ZeroMemory(&tm, sizeof(tm));
        const BOOL ok = GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        DeleteDC(dc);
        if (ok && tm.tmHeight > 0) {
            g_font = h;
            g_log.Inf(&g_log, "UI font: %ls", kFaces[i]);
            return;
        }
        DeleteObject(h);
    }
}

static void ApplyFontTree(HWND hwnd) {
    if (!g_font || !hwnd) return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        SendMessageW(child, WM_SETFONT, (WPARAM)g_font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void TranslateHwnd(HWND hwnd) {
    wchar_t buf[512];
    const int n = GetWindowTextW(hwnd, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    if (n > 0) {
        const wchar_t* tr = UiLookup(buf);
        if (tr) {
            SetWindowTextW(hwnd, tr);
            return;
        }
    }
    char ansi[512];
    if (GetWindowTextA(hwnd, ansi, (int)sizeof(ansi)) <= 0 || !ansi[0]) return;
    wchar_t sjis[512];
    if (ToWide(GAME_ACP, ansi, sjis, 512) <= 0) return;
    const wchar_t* tr = UiLookup(sjis);
    if (tr) SetWindowTextW(hwnd, tr);
}

static int MenuHasId(HMENU menu, UINT id) {
    return menu && GetMenuState(menu, id, MF_BYCOMMAND) != (UINT)-1;
}

void UiApplyMenu(HMENU menu) {
    if (!menu) return;
    EnsureTable();

    const int n = GetMenuItemCount(menu);
    for (int i = 0; i < n; ++i) {
        HMENU sub = GetSubMenu(menu, i);
        if (sub) {
            const wchar_t* title = NULL;
            if (MenuHasId(sub, 40001) || MenuHasId(sub, 40003) || MenuHasId(sub, 57603)) {
                title = L"文件(&F)";
            } else if (MenuHasId(sub, 57664) || MenuHasId(sub, 40007)) {
                title = L"帮助(&H)";
            } else if (MenuHasId(sub, 57643)) {
                title = L"编辑(&E)";
            }
            if (title) {
                ModifyMenuW(menu, (UINT)i, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)sub, title);
            }
            UiApplyMenu(sub);
        }
    }

    static const struct {
        UINT id;
        const wchar_t* cn;
        UINT need;
    } kCmd[] = {
        {57600, L"主界面(&N)", 40001},
        {57600, L"新建(&N)\tCtrl+N", 57601},
        {57603, L"保存(&S)", 0},
        {40003, L"加载(&L)", 0},
        {40001, L"设置(&C)", 0},
        {40002, L"显/隐对话框(&H)", 0},
        {40005, L"文本自动播放(&K)", 0},
        {40006, L"全屏模式(&F)", 0},
        {57665, L"退出(&X)", 0},
        {57664, L"版本信息(&A)...", 0},
        {40007, L"编解码器版本信息(&C)", 0},
        {57601, L"打开(&O)...\tCtrl+O", 0},
        {57604, L"另存为(&A)...", 0},
        {57643, L"撤销(&U)\tCtrl+Z", 0},
        {57635, L"剪切(&T)\tCtrl+X", 0},
        {57634, L"复制(&C)\tCtrl+C", 0},
        {57637, L"粘贴(&P)\tCtrl+V", 0},
        {57670, L"帮助(&H)", 0},
    };
    for (int i = 0; i < (int)(sizeof(kCmd) / sizeof(kCmd[0])); ++i) {
        if (kCmd[i].need && !MenuHasId(menu, kCmd[i].need)) continue;
        if (GetMenuState(menu, kCmd[i].id, MF_BYCOMMAND) == (UINT)-1) continue;
        ModifyMenuW(menu, kCmd[i].id, MF_BYCOMMAND | MF_STRING, kCmd[i].id, kCmd[i].cn);
    }
}

static void TranslateTabs(HWND hwnd) {
    HWND tab = FindWindowExW(hwnd, NULL, L"SysTabControl32", NULL);
    if (!tab) return;
    const int n = (int)SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        char ansi[256];
        TCITEMA it;
        ZeroMemory(&it, sizeof(it));
        it.mask = TCIF_TEXT;
        it.pszText = ansi;
        it.cchTextMax = (int)sizeof(ansi);
        if (!SendMessageA(tab, TCM_GETITEMA, (WPARAM)i, (LPARAM)&it) || !ansi[0]) continue;

        wchar_t wide[256];
        if (ToWide(GAME_ACP, ansi, wide, 256) <= 0) continue;
        const wchar_t* tr = UiLookup(wide);
        if (!tr) continue;

        TCITEMW tw;
        ZeroMemory(&tw, sizeof(tw));
        tw.mask = TCIF_TEXT;
        tw.pszText = (LPWSTR)tr;
        SendMessageW(tab, TCM_SETITEMW, (WPARAM)i, (LPARAM)&tw);
    }
}

void UiApplyDialog(HWND hwnd) {
    if (!hwnd) return;
    EnsureTable();
    TranslateHwnd(hwnd);
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        TranslateHwnd(child);
        child = GetWindow(child, GW_HWNDNEXT);
    }
    TranslateTabs(hwnd);
    HWND tab = FindWindowExA(hwnd, NULL, "SysTabControl32", NULL);
    if (tab) {
        wchar_t cur[256];
        const int n = GetWindowTextW(hwnd, cur, 256);
        int isConfig = 0;
        if (n > 0 && (wcsstr(cur, L"Properties") || wcsstr(cur, L"プロパティ") || wcsstr(cur, L"設定") ||
                      wcsstr(cur, kSheetTitle))) {
            isConfig = 1;
        } else {
            char ansi[256];
            if (GetWindowTextA(hwnd, ansi, 256) > 0 && (strstr(ansi, "Properties") || strstr(ansi, "設定"))) {
                isConfig = 1;
            }
        }
        if (isConfig && wcscmp(cur, kSheetTitle) != 0) SetWindowTextW(hwnd, kSheetTitle);
    }
    HMENU menu = GetMenu(hwnd);
    if (menu) {
        UiApplyMenu(menu);
        DrawMenuBar(hwnd);
    }
    char cls[32];
    if (GetClassNameA(hwnd, cls, (int)sizeof(cls)) && strcmp(cls, "#32770") == 0) ApplyFontTree(hwnd);
}

/* ---- PE resource Japanese -> Chinese (UTF-16, shrink-in-place) ---- */

static BYTE* RvaPtr(BYTE* base, DWORD rva) {
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (UINT i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const DWORD va = sec[i].VirtualAddress;
        const DWORD sz = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize
                                                                        : sec[i].SizeOfRawData;
        if (rva >= va && rva < va + sz) return base + rva;
    }
    return base + rva;
}

static DWORD Align4(DWORD p) {
    return (p + 3u) & ~3u;
}

static int ReadWsz(const BYTE* blob, DWORD size, DWORD* p, wchar_t* out, int outMax, int* ordinal) {
    if (*p + 2 > size) return 0;
    if (blob[*p] == 0xFF && blob[*p + 1] == 0xFF) {
        if (ordinal) *ordinal = 1;
        if (*p + 4 > size) return 0;
        if (outMax > 0) out[0] = 0;
        *p += 4;
        return 1;
    }
    if (ordinal) *ordinal = 0;
    int n = 0;
    while (*p + 2 <= size) {
        const wchar_t c = *(const wchar_t*)(blob + *p);
        *p += 2;
        if (!c) break;
        if (out && n + 1 < outMax) out[n] = c;
        ++n;
    }
    if (out && outMax > 0) {
        if (n >= outMax) n = outMax - 1;
        out[n] = 0;
    }
    return 1;
}

static DWORD WriteWsz(BYTE* out, DWORD cap, DWORD p, const wchar_t* s) {
    if (!s) s = L"";
    for (;;) {
        if (p + 2 > cap) return p;
        *(wchar_t*)(out + p) = *s;
        p += 2;
        if (!*s) break;
        ++s;
    }
    return p;
}

static int RebuildDialog(const BYTE* src, DWORD srcSize, BYTE* dst, DWORD dstCap, DWORD* outSize) {
    if (srcSize < 18 || dstCap < 18) return 0;
    DWORD sp = 0, dp = 0;
    const WORD sig = *(const WORD*)(src + 2);
    const int extra = sig == 0xFFFF;
    DWORD copyHdr = extra ? 26 : 18;
    if (copyHdr > srcSize || copyHdr > dstCap) return 0;
    memcpy(dst, src, copyHdr);
    const DWORD style = extra ? *(const DWORD*)(src + 12) : *(const DWORD*)src;
    const WORD cdit = extra ? *(const WORD*)(src + 16) : *(const WORD*)(src + 8);
    sp = copyHdr;
    dp = copyHdr;

    wchar_t buf[512];
    int ord = 0;
    for (int k = 0; k < 3; ++k) { /* menu, class, title */
        const DWORD save = sp;
        if (!ReadWsz(src, srcSize, &sp, buf, 512, &ord)) return 0;
        if (ord) {
            memcpy(dst + dp, src + save, sp - save);
            dp += sp - save;
        } else {
            const wchar_t* tr = (k == 2) ? UiLookup(buf) : NULL;
            dp = WriteWsz(dst, dstCap, dp, tr ? tr : buf);
        }
    }
    if (style & 0x40) { /* DS_SETFONT */
        if (extra) {
            if (sp + 6 > srcSize || dp + 6 > dstCap) return 0;
            memcpy(dst + dp, src + sp, 6);
            dp += 6;
            sp += 6;
        } else {
            if (sp + 2 > srcSize || dp + 2 > dstCap) return 0;
            memcpy(dst + dp, src + sp, 2);
            dp += 2;
            sp += 2;
        }
        const DWORD save = sp;
        if (!ReadWsz(src, srcSize, &sp, buf, 512, &ord)) return 0;
        if (ord) {
            memcpy(dst + dp, src + save, sp - save);
            dp += sp - save;
        } else {
            dp = WriteWsz(dst, dstCap, dp, buf);
        }
    }
    for (WORD i = 0; i < cdit; ++i) {
        sp = Align4(sp);
        dp = Align4(dp);
        const DWORD itemHdr = extra ? 24 : 18;
        if (sp + itemHdr > srcSize || dp + itemHdr > dstCap) return 0;
        memcpy(dst + dp, src + sp, itemHdr);
        dp += itemHdr;
        sp += itemHdr;
        for (int k = 0; k < 2; ++k) { /* class, title */
            const DWORD save = sp;
            if (!ReadWsz(src, srcSize, &sp, buf, 512, &ord)) return 0;
            if (ord) {
                memcpy(dst + dp, src + save, sp - save);
                dp += sp - save;
            } else {
                const wchar_t* tr = (k == 1) ? UiLookup(buf) : NULL;
                dp = WriteWsz(dst, dstCap, dp, tr ? tr : buf);
            }
        }
        if (sp + 2 > srcSize) return 0;
        WORD extraCount = *(const WORD*)(src + sp);
        if (sp + 2 + extraCount > srcSize || dp + 2 + extraCount > dstCap) return 0;
        memcpy(dst + dp, src + sp, 2 + extraCount);
        dp += 2 + extraCount;
        sp += 2 + extraCount;
    }
    *outSize = dp;
    return 1;
}

static int RebuildMenuLevel(const BYTE* src, DWORD srcSize, BYTE* dst, DWORD dstCap, DWORD* sp,
                            DWORD* dp) {
    for (;;) {
        if (*sp + 2 > srcSize || *dp + 2 > dstCap) return 0;
        const WORD flags = *(const WORD*)(src + *sp);
        *(WORD*)(dst + *dp) = flags;
        *sp += 2;
        *dp += 2;
        if (!(flags & 0x10)) {
            if (*sp + 2 > srcSize || *dp + 2 > dstCap) return 0;
            *(WORD*)(dst + *dp) = *(const WORD*)(src + *sp);
            *sp += 2;
            *dp += 2;
        }
        wchar_t buf[512];
        int ord = 0;
        const DWORD save = *sp;
        if (!ReadWsz(src, srcSize, sp, buf, 512, &ord)) return 0;
        if (ord) {
            memcpy(dst + *dp, src + save, *sp - save);
            *dp += *sp - save;
        } else {
            const wchar_t* tr = UiLookup(buf);
            *dp = WriteWsz(dst, dstCap, *dp, tr ? tr : buf);
        }
        if (flags & 0x10) {
            if (!RebuildMenuLevel(src, srcSize, dst, dstCap, sp, dp)) return 0;
        }
        if (flags & 0x80) break;
    }
    return 1;
}

static int RebuildMenu(const BYTE* src, DWORD srcSize, BYTE* dst, DWORD dstCap, DWORD* outSize) {
    if (srcSize < 4 || dstCap < 4) return 0;
    const WORD ver = *(const WORD*)src;
    if (ver == 1) return 0; /* MENUEX unused here */
    memcpy(dst, src, 4);
    DWORD sp = 4, dp = 4;
    if (!RebuildMenuLevel(src, srcSize, dst, dstCap, &sp, &dp)) return 0;
    *outSize = dp;
    return 1;
}

static int PatchBlob(BYTE* blob, DWORD size, int isMenu) {
    BYTE* tmp = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size + 32);
    if (!tmp) return 0;
    DWORD nsz = 0;
    const int ok = isMenu ? RebuildMenu(blob, size, tmp, size + 32, &nsz)
                          : RebuildDialog(blob, size, tmp, size + 32, &nsz);
    if (ok && nsz > 0 && nsz <= size) {
        DWORD old = 0;
        if (VirtualProtect(blob, size, PAGE_READWRITE, &old)) {
            memcpy(blob, tmp, nsz);
            if (nsz < size) ZeroMemory(blob + nsz, size - nsz);
            VirtualProtect(blob, size, old, &old);
        } else {
            HeapFreePtr(tmp);
            return 0;
        }
    }
    HeapFreePtr(tmp);
    return ok;
}

static void WalkRes(BYTE* base, DWORD dirRva, DWORD off, int level, int kind) {
    IMAGE_RESOURCE_DIRECTORY* dir = (IMAGE_RESOURCE_DIRECTORY*)RvaPtr(base, dirRva + off);
    const IMAGE_RESOURCE_DIRECTORY_ENTRY* ent =
        (IMAGE_RESOURCE_DIRECTORY_ENTRY*)((BYTE*)dir + sizeof(*dir));
    const int n = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (int i = 0; i < n; ++i) {
        const DWORD name = ent[i].Name;
        const DWORD offset = ent[i].OffsetToData;
        int nextKind = kind;
        if (level == 0 && !(name & 0x80000000)) {
            if (name == 4) nextKind = 4;      /* RT_MENU */
            else if (name == 5) nextKind = 5; /* RT_DIALOG */
            else nextKind = 0;
        }
        if (offset & 0x80000000) {
            WalkRes(base, dirRva, offset & 0x7FFFFFFF, level + 1, nextKind);
        } else if (nextKind == 4 || nextKind == 5) {
            IMAGE_RESOURCE_DATA_ENTRY* data = (IMAGE_RESOURCE_DATA_ENTRY*)RvaPtr(base, dirRva + offset);
            BYTE* blob = RvaPtr(base, data->OffsetToData);
            if (blob && data->Size) PatchBlob(blob, data->Size, nextKind == 4);
        }
    }
}

static void PatchResources(HMODULE exe) {
    BYTE* base = (BYTE*)exe;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
    if (!rva) return;
    EnsureTable();
    WalkRes(base, rva, 0, 0, 0);
    g_log.Suc(&g_log, "patched dialog/menu resources");
}

void InitUiText(void) {
    g_log = CreateLogger("UiText");
    EnsureTable();
    PickFont();
    HMODULE exe = GetModuleHandleW(NULL);
    if (exe) PatchResources(exe);
}
