#include "TransCodec.h"

#include <string.h>
#include <wctype.h>

void HeapFreePtr(void* p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

void* HeapDup(const void* src, size_t n) {
    void* p = HeapAlloc(GetProcessHeap(), 0, n);
    if (p && src) memcpy(p, src, n);
    return p;
}

wchar_t* DupUtf8(const char* s, int n) {
    if (!s || n < 0) return NULL;
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, s, n, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(wlen + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, n, w, wlen);
    w[wlen] = 0;
    return w;
}

int ToWide(UINT cp, const char* s, wchar_t* out, int outMax) {
    if (!s || !out || outMax < 2) return 0;
    const int n = MultiByteToWideChar(cp, 0, s, -1, out, outMax);
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return n - 1;
}

int ToNarrow(UINT cp, const wchar_t* s, char* out, int outMax) {
    if (!s || !out || outMax < 2) return 0;
    const int n = WideCharToMultiByte(cp, 0, s, -1, out, outMax, NULL, NULL);
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return n - 1;
}

int Plainize(const wchar_t* in, int n, wchar_t* out, int outMax) {
    int o = 0;
    for (int i = 0; i < n && o + 1 < outMax;) {
        if (in[i] == L'\\' && i + 1 < n && (in[i + 1] == L'n' || in[i + 1] == L'N')) {
            i += 2;
            continue;
        }
        if (iswspace(in[i])) {
            ++i;
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
    return o;
}

int EscapePercents(const wchar_t* in, wchar_t* out, int outMax) {
    int o = 0;
    for (int i = 0; in[i] && o + 2 < outMax; ++i) {
        out[o++] = in[i];
        if (in[i] == L'%') out[o++] = L'%';
    }
    out[o] = 0;
    return o;
}
