#include "zx_sharedmem.h"
#include "zx_pipe.h"
#include "zx_event.h"
#include "../core/zx_error.h"

#define SYS_IPC 27

extern int syscall4(int sys_num, int arg1, int arg2, int arg3, int arg4);
extern int syscall3(int sys_num, int arg1, int arg2, int arg3);
extern int syscall2(int sys_num, int arg1, int arg2);
extern int syscall1(int sys_num, int arg1);

/* Shared Memory */
HANDLE CreateSharedMemory(LPCSTR name, uint32_t size) {
    int res = syscall3(SYS_IPC, 0 /* IPC_SHM_CREATE */, (uint32_t)name, size);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}
void* MapSharedMemory(HANDLE hSharedMem) {
    int res = syscall2(SYS_IPC, 1 /* IPC_SHM_MAP */, (uint32_t)hSharedMem);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL; }
    return (void*)res;
}
BOOL UnmapSharedMemory(void* addr) {
    int res = syscall2(SYS_IPC, 2 /* IPC_SHM_UNMAP */, (uint32_t)addr);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

/* Pipe */
HANDLE CreatePipe(LPCSTR name) {
    int res = syscall2(SYS_IPC, 3 /* IPC_PIPE_CREATE */, (uint32_t)name);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}
BOOL ConnectPipe(HANDLE pipe) {
    int res = syscall2(SYS_IPC, 4 /* IPC_PIPE_CONNECT */, (uint32_t)pipe);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}
BOOL ReadPipe(HANDLE pipe, void* buffer, uint32_t size, uint32_t* bytes_read) {
    int res = syscall4(SYS_IPC, 5 /* IPC_PIPE_READ */, (uint32_t)pipe, (uint32_t)buffer, size);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_read) *bytes_read = (uint32_t)res;
    return TRUE;
}
BOOL WritePipe(HANDLE pipe, const void* buffer, uint32_t size, uint32_t* bytes_written) {
    int res = syscall4(SYS_IPC, 6 /* IPC_PIPE_WRITE */, (uint32_t)pipe, (uint32_t)buffer, size);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_written) *bytes_written = (uint32_t)res;
    return TRUE;
}

/* Event */
HANDLE CreateEvent(BOOL manual_reset, BOOL initial_state, LPCSTR name) {
    int res = syscall4(SYS_IPC, 7 /* IPC_EVENT_CREATE */, manual_reset, initial_state, (uint32_t)name);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}
BOOL SetEvent(HANDLE event) {
    int res = syscall2(SYS_IPC, 8 /* IPC_EVENT_SET */, (uint32_t)event);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}
BOOL ResetEvent(HANDLE event) {
    int res = syscall2(SYS_IPC, 9 /* IPC_EVENT_RESET */, (uint32_t)event);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}
