#ifndef DIALOGTEXT_H
#define DIALOGTEXT_H

/* SJIS dialog/name -> translated GBK. Returns 1 if dst was written. */
int TranslateSjisToGbk(const char* src, char* dst, int dstMax, int asFormat);
int TranslateNameSjisToGbk(const char* src, char* dst, int dstMax);

#endif
