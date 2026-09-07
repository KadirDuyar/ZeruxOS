#ifndef _ZX_SHAREDMEM_H
#define _ZX_SHAREDMEM_H

#include "../core/zx_types.h"

HANDLE CreateSharedMemory(LPCSTR name, uint32_t size);
void*  MapSharedMemory(HANDLE hSharedMem);
BOOL   UnmapSharedMemory(void* addr);

#endif /* _ZX_SHAREDMEM_H */
