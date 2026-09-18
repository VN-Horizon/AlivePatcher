#ifndef UITEXT_H
#define UITEXT_H

#include <windows.h>

void InitUiText(void);

/* SJIS (or ASCII) -> UTF-16, with UI translation. Caller HeapFree. */
wchar_t* UiDupTranslateA(LPCSTR s);

/* Copy PropertySheet header: Chinese caption, no " Properties" suffix. */
void* UiPatchPropertySheet(void* hdr);

void UiApplyDialog(HWND hwnd);
void UiApplyMenu(HMENU menu);

#endif
