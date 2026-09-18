#include "TransTable.h"

#include "TransCodec.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct TransEnt {
    wchar_t* key;
    wchar_t* val;
} TransEnt;

static TransEnt* g_ents;
static size_t g_nents;

static int EntCmp(const void* a, const void* b) {
    const TransEnt* ea = (const TransEnt*)a;
    const TransEnt* eb = (const TransEnt*)b;
    return wcscmp(ea->key ? ea->key : L"", eb->key ? eb->key : L"");
}

int TransTableLoad(const BYTE* data, DWORD size) {
    const BYTE* p = data;
    const BYTE* end = data + size;
    size_t cap = 0, n = 0;
    TransEnt* ents = NULL;

    while (p + 4 <= end) {
        DWORD klen = 0, vlen = 0;
        memcpy(&klen, p, 4);
        p += 4;
        if (p + klen + 4 > end) break;
        const char* key = (const char*)p;
        p += klen;
        memcpy(&vlen, p, 4);
        p += 4;
        if (p + vlen > end) break;
        const char* val = (const char*)p;
        p += vlen;

        if (klen == 7 && memcmp(key, "Column3", 7) == 0) continue;
        if (klen == 7 && memcmp(key, "Content", 7) == 0) continue;
        if (!klen || !vlen) continue;

        wchar_t* wk = DupUtf8(key, (int)klen);
        wchar_t* wv = DupUtf8(val, (int)vlen);
        if (!wk || !wv) {
            HeapFreePtr(wk);
            HeapFreePtr(wv);
            continue;
        }
        wchar_t plain[MAX_WIDE];
        if (Plainize(wk, (int)wcslen(wk), plain, MAX_WIDE) <= 0) {
            HeapFreePtr(wk);
            HeapFreePtr(wv);
            continue;
        }
        HeapFreePtr(wk);
        wchar_t* pk = (wchar_t*)HeapDup(plain, (wcslen(plain) + 1) * sizeof(wchar_t));
        if (!pk) {
            HeapFreePtr(wv);
            continue;
        }
        if (n == cap) {
            const size_t ncap = cap ? cap * 2 : 4096;
            TransEnt* neu = (TransEnt*)HeapAlloc(GetProcessHeap(), 0, ncap * sizeof(TransEnt));
            if (!neu) {
                HeapFreePtr(pk);
                HeapFreePtr(wv);
                break;
            }
            if (ents) {
                memcpy(neu, ents, n * sizeof(TransEnt));
                HeapFreePtr(ents);
            }
            ents = neu;
            cap = ncap;
        }
        ents[n].key = pk;
        ents[n].val = wv;
        ++n;
    }

    if (!ents || !n) {
        HeapFreePtr(ents);
        return 0;
    }
    qsort(ents, n, sizeof(TransEnt), EntCmp);

    size_t w = 0;
    for (size_t i = 0; i < n; ++i) {
        if (w && wcscmp(ents[w - 1].key, ents[i].key) == 0) {
            HeapFreePtr(ents[w - 1].val);
            ents[w - 1].val = ents[i].val;
            HeapFreePtr(ents[i].key);
        } else {
            ents[w++] = ents[i];
        }
    }
    g_ents = ents;
    g_nents = w;
    return 1;
}

unsigned TransTableCount(void) {
    return (unsigned)g_nents;
}

const wchar_t* TransLookup(const wchar_t* plain) {
    if (!plain || !plain[0] || !g_ents || !g_nents) return NULL;
    TransEnt key = {.key = (wchar_t*)plain, .val = NULL};
    const TransEnt* hit = (const TransEnt*)bsearch(&key, g_ents, g_nents, sizeof(TransEnt), EntCmp);
    return hit ? hit->val : NULL;
}

const wchar_t* TransLookupText(const wchar_t* text, int n) {
    wchar_t plain[MAX_WIDE];
    if (Plainize(text, n, plain, MAX_WIDE) <= 0) return NULL;
    return TransLookup(plain);
}
