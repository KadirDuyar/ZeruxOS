#include "zx_memory.h"
#include "../core/zx_error.h"

#define SYS_VIRTUAL_ALLOC   21
#define SYS_VIRTUAL_FREE    22
#define SYS_VIRTUAL_PROTECT 23

extern int syscall3(int sys_num, int arg1, int arg2, int arg3);

void* VirtualAlloc(void* addr, uint32_t size, DWORD alloc_type, DWORD protect) {
    if (size == 0) {
        SetLastError(ZX_ERROR_INVALID_PARAMETER);
        return NULL;
    }

    uint32_t flags = (alloc_type << 16) | (protect & 0xFFFF);
    int res = syscall3(SYS_VIRTUAL_ALLOC, (uint32_t)addr, size, flags);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return NULL;
    }
    
    return (void*)res;
}

BOOL VirtualFree(void* addr, uint32_t size, DWORD free_type) {
    if (!addr) {
        SetLastError(ZX_ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    int res = syscall3(SYS_VIRTUAL_FREE, (uint32_t)addr, size, free_type);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return FALSE;
    }
    
    return TRUE;
}

BOOL VirtualProtect(void* addr, uint32_t size, DWORD new_protect, DWORD* old_protect) {
    /* Pack new_protect and old_protect_ptr? old_protect is a pointer.
       We can pass addr, size, and a struct pointer.
       For now, just pass new_protect and ignore old_protect on kernel side. */
    int res = syscall3(SYS_VIRTUAL_PROTECT, (uint32_t)addr, size, new_protect);
    if (res < 0) {
        SetLastError((DWORD)(-res));
        return FALSE;
    }
    if (old_protect) *old_protect = ZX_MEM_READWRITE; /* Fake old protect */
    return TRUE;
}
