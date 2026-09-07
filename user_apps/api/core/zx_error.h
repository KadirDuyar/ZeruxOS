#ifndef _ZX_ERROR_H
#define _ZX_ERROR_H

#include "zx_types.h"

/* Temel Hata Kodlari (Windows uyumlu ve native eklemeler) */
#define ZX_SUCCESS                  0
#define ZX_ERROR_NOT_FOUND          2
#define ZX_ERROR_PATH_NOT_FOUND     3
#define ZX_ERROR_ACCESS_DENIED      5
#define ZX_ERROR_INVALID_HANDLE     6
#define ZX_ERROR_NO_MEMORY          8
#define ZX_ERROR_NOT_SUPPORTED      50
#define ZX_ERROR_INVALID_PARAMETER  87
#define ZX_ERROR_IO_DEVICE          1117
#define ZX_ERROR_TIMEOUT            1460

/* API Fonksiyonlari */
DWORD  GetLastError(void);
void   SetLastError(DWORD err_code);
LPCSTR GetErrorString(DWORD err_code);

#endif /* _ZX_ERROR_H */
