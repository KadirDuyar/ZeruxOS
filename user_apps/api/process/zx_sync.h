#ifndef _ZX_SYNC_H
#define _ZX_SYNC_H

#include "../core/zx_types.h"

#define ZX_WAIT_OBJECT_0 0
#define ZX_WAIT_TIMEOUT  258
#define ZX_WAIT_FAILED   0xFFFFFFFF
#define ZX_INFINITE      0xFFFFFFFF

HANDLE CreateMutex(BOOL initial_owner, LPCSTR name);
BOOL   ReleaseMutex(HANDLE mutex);

HANDLE CreateSemaphore(int initial_count, int max_count, LPCSTR name);
BOOL   ReleaseSemaphore(HANDLE semaphore, int release_count, int* previous_count);

DWORD  WaitForSingleObject(HANDLE handle, DWORD milliseconds);

#endif /* _ZX_SYNC_H */
