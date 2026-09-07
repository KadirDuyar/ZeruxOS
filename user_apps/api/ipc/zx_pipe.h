#ifndef _ZX_PIPE_H
#define _ZX_PIPE_H

#include "../core/zx_types.h"

HANDLE CreatePipe(LPCSTR name);
BOOL   ConnectPipe(HANDLE pipe);
BOOL   ReadPipe(HANDLE pipe, void* buffer, uint32_t size, uint32_t* bytes_read);
BOOL   WritePipe(HANDLE pipe, const void* buffer, uint32_t size, uint32_t* bytes_written);

#endif /* _ZX_PIPE_H */
