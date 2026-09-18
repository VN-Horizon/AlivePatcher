#ifndef INLINEHOOK_H
#define INLINEHOOK_H

#include <stddef.h>
#include <windows.h>

/*
 * Steal `stolen` prologue bytes (must be >= 5, complete instructions) and
 * JMP to hook. Returns an executable trampoline that runs the stolen bytes
 * then continues at src+stolen. NULL on mismatch or protect failure.
 */
void* InlineHook(void* src, void* hook, size_t stolen, const BYTE* expect);

#endif
