#include "../core/zx_types.h"
#include "../core/zx_error.h"

/* Syscall numaralari */
#define SYS_CLOSE_HANDLE 20

extern int syscall1(int sys_num, int arg1);

BOOL CloseHandle(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE || h == NULL_HANDLE) {
        SetLastError(ZX_ERROR_INVALID_HANDLE);
        return FALSE;
    }
    
    int res = syscall1(SYS_CLOSE_HANDLE, (uint32_t)h);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return FALSE;
    }
    
    return TRUE;
}
