#ifndef _ZX_MEMORY_H
#define _ZX_MEMORY_H

#include "../core/zx_types.h"

/* Bellek Koruma (Protection) Bayraklari */
#define ZX_MEM_NONE              0x00
#define ZX_MEM_READ              0x01
#define ZX_MEM_READWRITE         0x02
#define ZX_MEM_EXECUTE           0x04
#define ZX_MEM_EXECUTE_READ      0x05
#define ZX_MEM_EXECUTE_READWRITE 0x06

/* Tahsis (Allocation) Turleri */
#define ZX_MEM_COMMIT            0x1000
#define ZX_MEM_RESERVE           0x2000

/* Serbest Birakma (Free) Turleri */
#define ZX_MEM_RELEASE           0x8000
#define ZX_MEM_DECOMMIT          0x4000

/* Native ZXAPI Bellek Fonksiyonlari */
void* VirtualAlloc(void* addr, uint32_t size, DWORD alloc_type, DWORD protect);
BOOL  VirtualFree(void* addr, uint32_t size, DWORD free_type);
BOOL  VirtualProtect(void* addr, uint32_t size, DWORD new_protect, DWORD* old_protect);

#endif /* _ZX_MEMORY_H */
