#ifndef _ZX_IO_H
#define _ZX_IO_H

#include "../core/zx_types.h"

/* Dosya Acma Bayraklari (Creation Dispositions) */
#define ZX_CREATE_NEW        1
#define ZX_CREATE_ALWAYS     2
#define ZX_OPEN_EXISTING     3
#define ZX_OPEN_ALWAYS       4
#define ZX_TRUNCATE_EXISTING 5

/* Erisim Haklari (Access Rights) */
#define ZX_GENERIC_READ  0x80000000
#define ZX_GENERIC_WRITE 0x40000000

/* Dosya API */
HANDLE CreateFile(LPCSTR filename, DWORD access, DWORD creation_disposition);
BOOL   ReadFile(HANDLE file, void* buffer, DWORD bytes_to_read, DWORD* bytes_read_out);
BOOL   WriteFile(HANDLE file, const void* buffer, DWORD bytes_to_write, DWORD* bytes_written_out);
DWORD  SetFilePointer(HANDLE file, LONG distance, DWORD move_method);
BOOL   DeleteFile(LPCSTR filename);

#endif /* _ZX_IO_H */
