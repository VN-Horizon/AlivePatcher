#include "DialogText.h"

#include "TransCodec.h"
#include "TransTable.h"

#include <string.h>
#include <wctype.h>

static int SplitQuoted(const wchar_t* line, int len, int* prefixLen, int* contentOff, int* contentLen,
                       int* suffixOff) {
    int q = -1, eq = -1;
    for (int i = 0; i < len; ++i) {
        if (line[i] == 0x300C) {
            q = i;
            break;
        }
    }
    if (q <= 0) return 0;

    int cs = 0, ce = q;
    while (cs < ce && iswspace(line[cs])) ++cs;
    while (ce > cs && iswspace(line[ce - 1])) --ce;
    if (ce - cs <= 0 || ce - cs >= 10) return 0;

    for (int i = len - 1; i >= 0; --i) {
        if (line[i] == 0x300D) {
            eq = i;
            break;
        }
    }
    if (eq < 0 || eq <= q) {
        *prefixLen = q + 1;
        *contentOff = q + 1;
        *contentLen = len - (q + 1);
        *suffixOff = len;
        return 1;
    }
    *prefixLen = q + 1;
    *contentOff = q + 1;
    *contentLen = eq - (q + 1);
    *suffixOff = eq;
    return 1;
}

static int TrimWide(const wchar_t* s, int n, int* off, int* len) {
    int a = 0, b = n;
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    *off = a;
    *len = b - a;
    return *len;
}

static const wchar_t* LookupName(const wchar_t* s, int n) {
    const wchar_t* hit = TransLookupText(s, n);
    if (hit) return hit;

    int a = 0, b = n;
    while (a < b && (s[a] == 0x30FB || iswspace(s[a]))) ++a;
    while (b > a && (s[b - 1] == 0x30FB || iswspace(s[b - 1]))) --b;
    if (a == 0 && b == n) return NULL;
    return TransLookupText(s + a, b - a);
}

static int TranslatePrefix(const wchar_t* src, int prefixLen, wchar_t* dst, int dstMax) {
    int quote = prefixLen;
    if (quote > 0 && src[quote - 1] == 0x300C) --quote;

    const wchar_t* hit = LookupName(src, quote);
    if (!hit) {
        if (prefixLen > dstMax - 1) prefixLen = dstMax - 1;
        memcpy(dst, src, (size_t)prefixLen * sizeof(wchar_t));
        dst[prefixLen] = 0;
        return prefixLen;
    }

    int lead = 0;
    while (lead < quote && src[lead] == 0x30FB) ++lead;
    int tail = 0;
    while (quote - tail > lead && src[quote - 1 - tail] == 0x30FB) ++tail;

    int o = 0;
    for (int i = 0; i < lead && o + 1 < dstMax; ++i) dst[o++] = 0x30FB;
    for (int i = 0; hit[i] && o + 1 < dstMax; ++i) dst[o++] = hit[i];
    for (int i = 0; i < tail && o + 1 < dstMax; ++i) dst[o++] = 0x30FB;
    if (prefixLen > quote && o + 1 < dstMax) dst[o++] = 0x300C;
    dst[o] = 0;
    return o;
}

static int TranslateWide(const wchar_t* src, int srcLen, wchar_t* dst, int dstMax) {
    int prefixLen = 0, contentOff = 0, contentLen = 0, suffixOff = srcLen;
    const int quoted = SplitQuoted(src, srcLen, &prefixLen, &contentOff, &contentLen, &suffixOff);
    const wchar_t* hit = TransLookupText(src, srcLen);
    int useQuote = 0;
    if (!hit && quoted) {
        int toff = 0, tlen = 0;
        TrimWide(src + contentOff, contentLen, &toff, &tlen);
        hit = TransLookupText(src + contentOff + toff, tlen);
        useQuote = hit != NULL;
    }
    if (!hit) {
        if (srcLen >= dstMax) srcLen = dstMax - 1;
        memcpy(dst, src, (size_t)srcLen * sizeof(wchar_t));
        dst[srcLen] = 0;
        return srcLen;
    }

    int o = 0;
    if (useQuote) o = TranslatePrefix(src, prefixLen, dst, dstMax);
    for (int i = 0; hit[i] && o + 1 < dstMax; ++i) dst[o++] = hit[i];
    if (useQuote) {
        const int suffixLen = srcLen - suffixOff;
        int remain = dstMax - 1 - o;
        if (remain < 0) remain = 0;
        const int n = suffixLen < remain ? suffixLen : remain;
        memcpy(dst + o, src + suffixOff, (size_t)n * sizeof(wchar_t));
        o += n;
    }
    dst[o] = 0;
    return o;
}

int TranslateSjisToGbk(const char* src, char* dst, int dstMax, int asFormat) {
    wchar_t wide[MAX_WIDE];
    wchar_t translated[MAX_WIDE];
    wchar_t escaped[MAX_WIDE];
    if (!src || !dst || dstMax < 2) return 0;

    const int wn = ToWide(GAME_ACP, src, wide, MAX_WIDE);
    if (wn <= 0) return 0;

    TranslateWide(wide, wn, translated, MAX_WIDE);
    const wchar_t* outw = translated;
    if (asFormat) {
        EscapePercents(translated, escaped, MAX_WIDE);
        outw = escaped;
    }
    if (ToNarrow(CN_ACP, outw, dst, dstMax) <= 0) return 0;
    dst[dstMax - 1] = 0;
    return 1;
}

int TranslateNameSjisToGbk(const char* src, char* dst, int dstMax) {
    wchar_t wide[MAX_NAME];
    if (!src || !dst || dstMax < 2) return 0;

    const int wn = ToWide(GAME_ACP, src, wide, MAX_NAME);
    if (wn <= 0) return 0;

    const wchar_t* hit = LookupName(wide, wn);
    const wchar_t* outw = hit ? hit : wide;
    if (ToNarrow(CN_ACP, outw, dst, dstMax) <= 0) return 0;
    dst[dstMax - 1] = 0;
    return 1;
}
