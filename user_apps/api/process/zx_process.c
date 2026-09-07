#include "zx_process.h"
#include "../core/zx_error.h"

/* Mevcut/Yeni Syscall Numaralari */
#define SYS_GETPID      2
#define SYS_EXIT        5
#define SYS_SPAWN       11
#define SYS_WAITPID     12
#define SYS_PROCESS_EXT 24 /* Expanded process syscalls */

extern int syscall0(int sys_num);
extern int syscall1(int sys_num, int arg1);
extern int syscall2(int sys_num, int arg1, int arg2);
extern int syscall3(int sys_num, int arg1, int arg2, int arg3);

HANDLE CreateProcess(LPCSTR path, LPCSTR args) {
    /* Mevcut spawn() kernel'de PID donuyor. İleride Object Manager icinde HANDLE donecek.
       Simdilik pid'i donduren spawn syscall'unu kullaniyoruz, ancak Handle Manager aktif
       oldugunda bu syscall (veya kernel kodu) Process objesi olusturup handle donecek. */
    const char *argv[3];
    argv[0] = path;
    argv[1] = args;
    argv[2] = NULL;
    
    int res = syscall2(SYS_SPAWN, (uint32_t)path, (uint32_t)argv);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return NULL_HANDLE;
    }
    return (HANDLE)res; /* Gecici: Handle yerine pid donuluyor */
}

BOOL TerminateProcess(HANDLE process, int exit_code) {
    int res = syscall3(SYS_PROCESS_EXT, 1 /* action = terminate */, (uint32_t)process, exit_code);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return FALSE;
    }
    return TRUE;
}

DWORD GetCurrentProcessId(void) {
    return (DWORD)syscall0(SYS_GETPID);
}

HANDLE GetCurrentProcess(void) {
    /* Gecici: Kernel henuz GetCurrentProcess() handle donmuyor. */
    return (HANDLE)GetCurrentProcessId();
}

DWORD GetProcessId(HANDLE process) {
    /* Eger process objesi gercek bir Handle ise SYS_PROCESS_EXT ile ID istenir.
       Simdilik handle'in dogrudan pid oldugunu varsayiyoruz (gecis evresi). */
    return (DWORD)process;
}

BOOL WaitForProcess(HANDLE process) {
    int res = syscall1(SYS_WAITPID, (uint32_t)process);
    if (res < 0) {
        SetLastError(ZX_ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

void ExitProcess(int exit_code) {
    syscall1(SYS_EXIT, exit_code);
    while (1);
}
