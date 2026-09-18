#ifndef WINMMPROXY_H
#define WINMMPROXY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Load real winmm (extracted cache, else System32) and bind exports into jmp stubs. */
int InitWinmmProxy(void);
HMODULE WinmmOrgModule(void);

#endif
