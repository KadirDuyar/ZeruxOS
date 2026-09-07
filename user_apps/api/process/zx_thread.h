#ifndef _ZX_THREAD_H
#define _ZX_THREAD_H

#include "../core/zx_types.h"

typedef DWORD (*ZX_THREAD_START_ROUTINE)(void* parameter);

HANDLE CreateThread(ZX_THREAD_START_ROUTINE start_address, void* parameter, DWORD creation_flags, DWORD* thread_id);
void   ExitThread(DWORD exit_code);
DWORD  GetCurrentThreadId(void);
HANDLE GetCurrentThread(void);
BOOL   SuspendThread(HANDLE thread);
BOOL   ResumeThread(HANDLE thread);
void   Sleep(DWORD ms);
void   YieldProcessor(void);

#endif /* _ZX_THREAD_H */
