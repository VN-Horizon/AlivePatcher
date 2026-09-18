#ifndef BACKLOG_H
#define BACKLOG_H

#include <wchar.h>

void InitBacklog(void);
void BacklogSetName(const wchar_t* name);
void BacklogAdd(const wchar_t* line);

#endif
