#ifndef TRANSCODEC_H
#define TRANSCODEC_H

#include <windows.h>

enum {
    GAME_ACP = 932, /* Shift-JIS */
    CN_ACP = 936,   /* GBK */
    MAX_LINE = 1024,
    MAX_WIDE = 1024,
    MAX_NAME = 256,
};

void HeapFreePtr(void* p);
void* HeapDup(const void* src, size_t n);
wchar_t* DupUtf8(const char* s, int n);
int ToWide(UINT cp, const char* s, wchar_t* out, int outMax);
int ToNarrow(UINT cp, const wchar_t* s, char* out, int outMax);
int Plainize(const wchar_t* in, int n, wchar_t* out, int outMax);
int EscapePercents(const wchar_t* in, wchar_t* out, int outMax);

#endif
