#include "zx_sync.h"
#include "../core/zx_error.h"

#define SYS_SYNC 26 /* Unified syscall for sync objects */

extern int syscall4(int sys_num, int arg1, int arg2, int arg3, int arg4);
extern int syscall3(int sys_num, int arg1, int arg2, int arg3);

HANDLE CreateMutex(BOOL initial_owner, LPCSTR name) {
    int res = syscall3(SYS_SYNC, 0 /* CREATE_MUTEX */, initial_owner, (uint32_t)name);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}

BOOL ReleaseMutex(HANDLE mutex) {
    int res = syscall3(SYS_SYNC, 1 /* RELEASE_MUTEX */, (uint32_t)mutex, 0);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

HANDLE CreateSemaphore(int initial_count, int max_count, LPCSTR name) {
    int res = syscall4(SYS_SYNC, 2 /* CREATE_SEMAPHORE */, initial_count, max_count, (uint32_t)name);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}

BOOL ReleaseSemaphore(HANDLE semaphore, int release_count, int* previous_count) {
    int res = syscall4(SYS_SYNC, 3 /* RELEASE_SEMAPHORE */, (uint32_t)semaphore, release_count, (uint32_t)previous_count);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    int res = syscall3(SYS_SYNC, 4 /* WAIT_SINGLE_OBJECT */, (uint32_t)handle, milliseconds);
    if (res < 0) {
        if (res == -ZX_ERROR_TIMEOUT) return ZX_WAIT_TIMEOUT;
        SetLastError((DWORD)(-res));
        return ZX_WAIT_FAILED;
    }
    return ZX_WAIT_OBJECT_0;
}
