#include "AlivePatcher.h"

#include <stdbool.h>

#include "CdAudio.h"
#include "CdCheck.h"
#include "CodePage.h"
#include "ConfigDialog.h"
#include "Embed.h"
#include "ModConsole.h"
#include "Translator.h"
#include "VoiceAcm.h"
#include "WinmmProxy.h"

#include <stdio.h>
#include <windows.h>

#include "Logger.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    const Logger logger = CreateLogger("AlivePatcher");
    FILE* file = fopen(".nodebug", "rb");
    const BOOL a = !file;
    BOOL b = false;
    if (!a) {
        char buf[1];
        b = fread(buf, 1, 1, file) > 0;
        fclose(file);
    }

    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            EmbedSetModule(hModule);
            /* Must init before any export stub is called (no PE forwards). */
            if (!InitWinmmProxy()) return FALSE;
            if (!b) InitDebugConsole();

            logger.Inf(&logger, "");
            logger.Inf(&logger, "░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░");
            logger.Inf(&logger, "░█▀█░█░░░▀█▀░█░█░█▀▀░█▀█░█▀█░▀█▀░█▀▀░█░█░█▀▀░█▀▄░");
            logger.Inf(&logger, "░█▀█░█░░░░█░░▀▄▀░█▀▀░█▀▀░█▀█░░█░░█░░░█▀█░█▀▀░█▀▄░");
            logger.Inf(&logger, "░▀░▀░▀▀▀░▀▀▀░░▀░░▀▀▀░▀░░░▀░▀░░▀░░▀▀▀░▀░▀░▀▀▀░▀░▀░");
            logger.Inf(&logger, "░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░");
            logger.Inf(&logger, "");
            logger.Wrn(&logger, "By 兄虐汉化组/VN Horizon V1.0");
            logger.Inf(&logger, "");
            logger.Inf(&logger, "如果您在游玩过程中遇到任何问题，爱用用不用滚。");
            logger.Inf(&logger, "https://github.com/VN-Horizon/AlivePatcher/issues");
            logger.Inf(&logger, "");
            if (a)
                logger.Suc(&logger, "觉得此日志窗口很烦？试试在游戏根目录创建.nodebug文件");
            else
                logger.Err(&logger, "你以为创建.nodebug就够了吗？你个傻逼");

            InitVoiceAcm();
            /* Installer runs on a worker after DllMain returns; do not init audio yet. */
            if (VoiceAcmIsRestarting()) break;
            InitShiftJisCompat();
            InitConfigDialog();
            InitCdCheck();
            InitCdAudio();
            InitTranslator();
            break;
        }

        case DLL_PROCESS_DETACH:
            ShutdownCdAudio();
            FreeConsole();
            break;
        default: ;
    }
    return TRUE;
}
