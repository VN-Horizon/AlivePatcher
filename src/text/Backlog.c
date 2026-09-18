#include "Backlog.h"

#include "Logger.h"
#include "TransCodec.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>

enum { NAME_WIDTH = 10 };

static Logger g_log;
static int g_started;
static wchar_t g_setName[MAX_NAME];
static wchar_t g_lastName[MAX_NAME];
static wchar_t g_lastText[MAX_WIDE];

static int WideWidth(const wchar_t* s) {
    int w = 0;
    if (!s) return 0;
    for (; *s; ++s) w += (*s > 0xFF) ? 2 : 1;
    return w;
}

static int TrimRange(const wchar_t* s, int n, int* off, int* len) {
    int a = 0, b = n;
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    *off = a;
    *len = b - a;
    return *len;
}

static void CopyWide(wchar_t* dst, int dstMax, const wchar_t* src, int n) {
    if (n >= dstMax) n = dstMax - 1;
    if (n < 0) n = 0;
    if (n > 0) memmove(dst, src, (size_t)n * sizeof(wchar_t));
    dst[n] = 0;
}

static int IsNameJunk(wchar_t c) {
    return c == L'?' || c == L'.' || c == L'*' || c == 0x00B7 || c == 0x2022 || c == 0x30A0 ||
           c == 0x30FB || c == 0xFF65 || c == 0x3000 || iswspace(c);
}

static void CleanName(wchar_t* s) {
    wchar_t tmp[MAX_NAME];
    int n = (int)wcslen(s);
    int a = 0, b = n, o = 0;
    while (a < b && IsNameJunk(s[a])) ++a;
    while (b > a && IsNameJunk(s[b - 1])) --b;
    for (int i = a; i < b && o + 1 < MAX_NAME; ++i) {
        if (IsNameJunk(s[i])) continue;
        tmp[o++] = s[i];
    }
    tmp[o] = 0;
    CopyWide(s, MAX_NAME, tmp, o);
}

static void StripTrailingNewlines(wchar_t* s) {
    int n = (int)wcslen(s);
    while (n > 0 && (s[n - 1] == L'\n' || s[n - 1] == L'\r')) s[--n] = 0;
}

static void NormalizeText(const wchar_t* in, wchar_t* out, int outMax) {
    int o = 0;
    for (int i = 0; in[i] && o + 1 < outMax;) {
        if (in[i] == L'%' && in[i + 1] == L'%') {
            out[o++] = L'%';
            i += 2;
            continue;
        }
        if (in[i] == L'\\' && (in[i + 1] == L'n' || in[i + 1] == L'N')) {
            out[o++] = L'\n';
            i += 2;
            continue;
        }
        if (in[i] == L'\r') {
            ++i;
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
    StripTrailingNewlines(out);
}

static void SplitSpeaker(const wchar_t* line, wchar_t* name, int nameMax, wchar_t* text, int textMax) {
    name[0] = 0;
    text[0] = 0;
    if (!line || !line[0]) return;

    const int len = (int)wcslen(line);
    int q = -1, eq = -1;
    for (int i = 0; i < len; ++i) {
        if (line[i] == 0x300C) {
            q = i;
            break;
        }
    }
    if (q > 0) {
        int off = 0, nlen = 0;
        if (TrimRange(line, q, &off, &nlen) > 0 && nlen < 10) {
            CopyWide(name, nameMax, line + off, nlen);
            CleanName(name);
        }
        for (int i = len - 1; i > q; --i) {
            if (line[i] == 0x300D) {
                eq = i;
                break;
            }
        }
        const int contentOff = q + 1;
        const int contentLen = (eq > q) ? (eq - contentOff) : (len - contentOff);
        int toff = 0, tlen = 0;
        TrimRange(line + contentOff, contentLen, &toff, &tlen);
        CopyWide(text, textMax, line + contentOff + toff, tlen);
        return;
    }

    int off = 0, n = 0;
    TrimRange(line, len, &off, &n);
    CopyWide(text, textMax, line + off, n);
}

static int ToUtf8(const wchar_t* s, char* out, int outMax) {
    if (!s || !s[0]) {
        if (outMax > 0) out[0] = 0;
        return 0;
    }
    return ToNarrow(CP_UTF8, s, out, outMax);
}

static void EnsureStart(void) {
    if (g_started) return;
    g_started = 1;
    g_log.Wrn(&g_log, "");
    g_log.Wrn(&g_log, "=========================================");
    g_log.Wrn(&g_log, "============ 这里是Backlog喵 ============");
    g_log.Wrn(&g_log, "=========================================");
    g_log.Wrn(&g_log, "");
}

static WORD ConsoleColor(HANDLE console) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(console, &info)) {
        return info.wAttributes;
    }
    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

static void EmitDialogLine(const wchar_t* name, const wchar_t* text, int showName) {
    char utf8[MAX_WIDE * 3];
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    const WORD old = ConsoleColor(console);
    const WORD white = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    const WORD nameColor = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    if (ToUtf8(text, utf8, (int)sizeof(utf8)) <= 0) return;

    if (console != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(console, white);
    printf("[%s] ", g_log.name ? g_log.name : "BACKLOG");

    if (name && name[0]) {
        int pad = NAME_WIDTH - WideWidth(name);
        if (pad < 0) pad = 0;
        if (showName) {
            char nameUtf8[MAX_NAME * 3];
            if (ToUtf8(name, nameUtf8, (int)sizeof(nameUtf8)) <= 0) {
                if (console != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(console, old);
                return;
            }
            if (console != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(console, nameColor);
            printf("%s", nameUtf8);
            if (console != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(console, white);
            printf("%*s: ", pad, "");
        } else {
            printf("%*s", NAME_WIDTH + 2, "");
        }
    }

    printf("%s\n", utf8);
    fflush(stdout);
    if (console != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(console, old);
}

static void EmitBlock(const wchar_t* name, const wchar_t* text, int showName) {
    const wchar_t* p = text;
    int first = 1;

    while (*p) {
        const wchar_t* nl = wcschr(p, L'\n');
        wchar_t line[MAX_WIDE];
        int n = nl ? (int)(nl - p) : (int)wcslen(p);
        CopyWide(line, MAX_WIDE, p, n);
        p += n;
        if (*p == L'\n') ++p;

        int off = 0, len = 0;
        if (TrimRange(line, (int)wcslen(line), &off, &len) <= 0) continue;
        CopyWide(line, MAX_WIDE, line + off, len);

        EnsureStart();
        EmitDialogLine(name, line, showName && first);
        first = 0;
    }
}

void InitBacklog(void) {
    g_log = CreateLogger("BACKLOG");
    g_started = 0;
    g_setName[0] = 0;
    g_lastName[0] = 0;
    g_lastText[0] = 0;
}

void BacklogSetName(const wchar_t* name) {
    if (!name) name = L"";
    CopyWide(g_setName, MAX_NAME, name, (int)wcslen(name));
    CleanName(g_setName);
}

void BacklogAdd(const wchar_t* line) {
    wchar_t name[MAX_NAME];
    wchar_t raw[MAX_WIDE];
    wchar_t text[MAX_WIDE];
    if (!line || !line[0]) return;

    SplitSpeaker(line, name, MAX_NAME, raw, MAX_WIDE);
    if (!name[0] && g_setName[0]) CopyWide(name, MAX_NAME, g_setName, (int)wcslen(g_setName));
    CleanName(name);
    NormalizeText(raw, text, MAX_WIDE);
    if (!text[0]) return;

    if (wcscmp(name, g_lastName) == 0 && wcscmp(text, g_lastText) == 0) return;

    const int same = wcscmp(name, g_lastName) == 0 && g_lastText[0];
    EmitBlock(name, text, !same && name[0]);

    CopyWide(g_lastName, MAX_NAME, name, (int)wcslen(name));
    CopyWide(g_lastText, MAX_WIDE, text, (int)wcslen(text));
}
