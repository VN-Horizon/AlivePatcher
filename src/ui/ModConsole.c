#include "ModConsole.h"
#include "Logger.h"

#include <stdio.h>
#include <wchar.h>
#include <windows.h>

static void SetConsoleMonoFont(void) {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE) return;

    CONSOLE_FONT_INFOEX info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.dwFontSize.Y = 17;
    info.FontFamily = FF_MODERN | FIXED_PITCH;
    info.FontWeight = FW_NORMAL;
    wcsncpy(info.FaceName, L"Consolas", LF_FACESIZE - 1);
    SetCurrentConsoleFontEx(console, FALSE, &info);
}

void InitDebugConsole() {
    if (GetConsoleWindow() != NULL || !AllocConsole()) return;

    SetConsoleTitleW(L"AlivePatcher By 兄虐汉化组");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleMonoFont();

    FILE* fp = NULL;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    const Logger logger = CreateLogger("ModConsole");
    logger.Suc(&logger, "Console Initialized");
}
