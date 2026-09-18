#include "Logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

typedef enum {
    LOG_SUC = 0,
    LOG_INF,
    LOG_WRN,
    LOG_ERR
} LogLevel;

static WORD LevelColor(const LogLevel level) {
    switch (level) {
        case LOG_SUC: return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LOG_INF: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        case LOG_WRN: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LOG_ERR: return FOREGROUND_RED | FOREGROUND_INTENSITY;
        default:      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

static void LoggerWrite(const Logger* self, const LogLevel level, const char* fmt, const va_list args) {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    const WORD color = LevelColor(level);
    CONSOLE_SCREEN_BUFFER_INFO info;
    WORD old_attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    if (console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(console, &info)) {
        old_attrs = info.wAttributes;
        SetConsoleTextAttribute(console, color);
    }

    printf("[%s] ", self && self->name ? self->name : "?");
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);

    if (console != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(console, old_attrs);
    }
}

static void LoggerSuc(const Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LoggerWrite(self, LOG_SUC, fmt, args);
    va_end(args);
}

static void LoggerInf(const Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LoggerWrite(self, LOG_INF, fmt, args);
    va_end(args);
}

static void LoggerWrn(const Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LoggerWrite(self, LOG_WRN, fmt, args);
    va_end(args);
}

static void LoggerErr(const Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LoggerWrite(self, LOG_ERR, fmt, args);
    va_end(args);
}

Logger CreateLogger(const char* name) {
    const Logger logger = {
        .name = name,
        .Suc = LoggerSuc,
        .Inf = LoggerInf,
        .Wrn = LoggerWrn,
        .Err = LoggerErr,
    };
    return logger;
}
