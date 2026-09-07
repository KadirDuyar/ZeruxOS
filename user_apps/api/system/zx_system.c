#include "zx_time.h"
#include "zx_system.h"
#include "zx_env.h"
#include "../core/zx_error.h"

#define SYS_TIME    30
#define SYS_INFO    31
#define SYS_ENV     32

extern int syscall2(int sys_num, int arg1, int arg2);
extern int syscall1(int sys_num, int arg1);
extern int syscall0(int sys_num);

/* Time API */
DWORD GetTickCount(void) {
    return (DWORD)syscall1(SYS_TIME, 0 /* GET_TICK_COUNT */);
}

BOOL GetSystemTime(SYSTEMTIME *time) {
    int res = syscall2(SYS_TIME, 1 /* GET_SYSTEM_TIME */, (uint32_t)time);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

BOOL GetLocalTime(SYSTEMTIME *time) {
    return GetSystemTime(time); /* Simdilik ayni */
}

/* System API */
BOOL GetSystemInfo(SYSTEM_INFO *info) {
    int res = syscall2(SYS_INFO, 0 /* GET_SYS_INFO */, (uint32_t)info);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

/* Environment API (Stub) */
LPCSTR GetEnvironmentVariable(LPCSTR name) {
    /* İleride process control block icindeki env arrayine erisecek */
    (void)name;
    return NULL;
}
BOOL SetEnvironmentVariable(LPCSTR name, LPCSTR value) {
    (void)name; (void)value;
    return FALSE;
}
LPCSTR GetCurrentDirectory(void) {
    /* syscall32 GET_CWD */
    return "/"; /* stub */
}
BOOL SetCurrentDirectory(LPCSTR path) {
    (void)path;
    return FALSE;
}
LPCSTR GetCommandLine(void) {
    return "";
}
