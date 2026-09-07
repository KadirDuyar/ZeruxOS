#ifndef _ZX_TIME_H
#define _ZX_TIME_H

#include "../core/zx_types.h"

typedef struct {
    WORD year;
    WORD month;
    WORD day;
    WORD hour;
    WORD minute;
    WORD second;
    WORD millisecond;
} SYSTEMTIME;

DWORD GetTickCount(void);
BOOL  GetSystemTime(SYSTEMTIME *time);
BOOL  GetLocalTime(SYSTEMTIME *time);
void  Sleep(DWORD ms); /* Already in zx_thread.h, duplicated here optionally or just re-include */

#endif /* _ZX_TIME_H */
