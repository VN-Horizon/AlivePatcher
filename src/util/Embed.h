#ifndef EMBED_H
#define EMBED_H

#include <windows.h>

/* Call once from DllMain with this module's HMODULE. */
void EmbedSetModule(HMODULE self);

/* Lock RCDATA by name (e.g. "M01", "WINMM_ORG"). Pointer valid while module loaded. */
int EmbedGet(const char* name, const void** data, DWORD* size);

/* Write resource to path (CREATE_ALWAYS). */
int EmbedExtract(const char* name, const char* path);

/*
 * Extract next to this DLL (same directory).
 * outPath optional. Returns 1 on success.
 */
int EmbedExtractBesideSelf(const char* name, const char* fileName, char* outPath, size_t outSz);

/*
 * Ensure winmm_org.dll is on a writable path (AppData, else TEMP).
 * outPath optional. Returns 1 on success.
 */
int EmbedEnsureWinmmOrg(char* outPath, size_t outSz);

#endif
