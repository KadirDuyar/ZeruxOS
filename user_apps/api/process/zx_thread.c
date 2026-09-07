#include "zx_thread.h"
#include "../core/zx_error.h"

#define SYS_YIELD    3
#define SYS_SLEEP    4
#define SYS_THREAD   25 /* Expanded thread syscalls */

extern int syscall0(int sys_num);
extern int syscall1(int sys_num, int arg1);
extern int syscall4(int sys_num, int arg1, int arg2, int arg3, int arg4);

HANDLE CreateThread(ZX_THREAD_START_ROUTINE start_address, void* parameter, DWORD creation_flags, DWORD* thread_id) {
    int res = syscall4(SYS_THREAD, 0 /* action: create */, (uint32_t)start_address, (uint32_t)parameter, creation_flags);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return NULL_HANDLE;
    }
    if (thread_id) {
        *thread_id = (DWORD)res;
    }
    return (HANDLE)res;
}

void ExitThread(DWORD exit_code) {
    syscall1(SYS_THREAD, 1 /* action: exit */);
    while(1);
}

DWORD GetCurrentThreadId(void) {
    return (DWORD)syscall0(SYS_THREAD /* action: get_id implicitly or we can use another syscall */);
}

HANDLE GetCurrentThread(void) {
    return (HANDLE)GetCurrentThreadId();
}

BOOL SuspendThread(HANDLE thread) {
    int res = syscall4(SYS_THREAD, 2 /* action: suspend */, (uint32_t)thread, 0, 0);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

BOOL ResumeThread(HANDLE thread) {
    int res = syscall4(SYS_THREAD, 3 /* action: resume */, (uint32_t)thread, 0, 0);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

void Sleep(DWORD ms) {
    syscall1(SYS_SLEEP, ms);
}

void YieldProcessor(void) {
    syscall0(SYS_YIELD);
}
