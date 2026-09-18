#ifndef WINMMPROXY_H
#define WINMMPROXY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Extract embedded winmm_org.dll and bind all of its exports into jmp stubs. */
int InitWinmmProxy(void);
HMODULE WinmmOrgModule(void);

#endif
