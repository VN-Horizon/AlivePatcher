#ifndef TRANSTABLE_H
#define TRANSTABLE_H

#include <windows.h>

int TransTableLoad(const BYTE* data, DWORD size);
unsigned TransTableCount(void);
const wchar_t* TransLookup(const wchar_t* plain);
const wchar_t* TransLookupText(const wchar_t* text, int n);

#endif
