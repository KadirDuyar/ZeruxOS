#ifndef _ZX_PROCESS_H
#define _ZX_PROCESS_H

#include "../core/zx_types.h"

/* Process Bilgi Yapisı */
typedef struct {
    DWORD pid;
    char name[64];
    DWORD state;
    DWORD memory_usage;
} PROCESS_INFO;

/* Process API'leri */
HANDLE CreateProcess(LPCSTR path, LPCSTR args);
BOOL   TerminateProcess(HANDLE process, int exit_code);
DWORD  GetCurrentProcessId(void);
HANDLE GetCurrentProcess(void);
DWORD  GetProcessId(HANDLE process);
BOOL   WaitForProcess(HANDLE process);
BOOL   IsProcessRunning(HANDLE process);
void   ExitProcess(int exit_code);
BOOL   GetProcessInfo(DWORD pid, PROCESS_INFO *info);

#endif /* _ZX_PROCESS_H */
