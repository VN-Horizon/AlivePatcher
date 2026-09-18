#ifndef LOGGER_H
#define LOGGER_H

typedef struct Logger Logger;

struct Logger {
    const char* name;
    void (*Suc)(const Logger* self, const char* fmt, ...);
    void (*Inf)(const Logger* self, const char* fmt, ...);
    void (*Wrn)(const Logger* self, const char* fmt, ...);
    void (*Err)(const Logger* self, const char* fmt, ...);
};

Logger CreateLogger(const char* name);

#endif
