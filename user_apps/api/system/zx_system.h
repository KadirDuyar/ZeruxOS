#ifndef _ZX_SYSTEM_H
#define _ZX_SYSTEM_H

#include "../core/zx_types.h"

typedef struct {
    char kernel_version[32];
    DWORD total_memory;
    DWORD free_memory;
    DWORD uptime_seconds;
} SYSTEM_INFO;

BOOL GetSystemInfo(SYSTEM_INFO *info);

#endif /* _ZX_SYSTEM_H */
