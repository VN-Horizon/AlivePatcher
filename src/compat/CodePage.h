#ifndef CODEPAGE_H
#define CODEPAGE_H

/* Force Shift-JIS (CP932) for ANSI APIs so the game works on UTF-8 system locales. */
void InitShiftJisCompat(void);

#endif
