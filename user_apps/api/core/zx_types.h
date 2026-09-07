#ifndef _ZX_TYPES_H
#define _ZX_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Standart API Tipleri */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  BOOL;
typedef int32_t  INT;
typedef uint32_t UINT;
typedef int32_t  LONG;
typedef uint32_t ULONG;

/* Obje Handle'lari */
typedef uint32_t HANDLE;  /* Kernel Obje yoneticisi referansi */
typedef uint32_t HWND;    /* GUI Pencere referansi */

/* Parametreler */
typedef uint32_t WPARAM;
typedef uint32_t LPARAM;

/* Stringler (ZXAPI, UTF-8 standardini benimser) */
typedef char*       LPSTR;
typedef const char* LPCSTR;

/* Sabitler */
#define TRUE  1
#define FALSE 0
#ifndef NULL
#define NULL ((void*)0)
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)0xFFFFFFFF)
#define NULL_HANDLE          ((HANDLE)0)

#endif /* _ZX_TYPES_H */
