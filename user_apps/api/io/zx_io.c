#include "zx_io.h"
#include "zx_device.h"
#include "../core/zx_error.h"
#include "../core/zx_handle.h"

#define SYS_OPEN    6
#define SYS_READ    7
#define SYS_WRITE   1
#define SYS_DEVICE_EXT 28 /* Extended Device IOCTL syscall */

extern int syscall4(int sys_num, int arg1, int arg2, int arg3, int arg4);
extern int syscall3(int sys_num, int arg1, int arg2, int arg3);
extern int syscall2(int sys_num, int arg1, int arg2);
extern int syscall1(int sys_num, int arg1);

/* Dosya */
HANDLE CreateFile(LPCSTR filename, DWORD access, DWORD creation_disposition) {
    /* TODO: access ve creation_disposition mapping. 
       Su anki SYS_OPEN syscall O_RDONLY (0) gibi linux uyumlu flag bekliyor.
       Basit bir mappleme yapiyoruz. */
    int flags = 0; /* O_RDONLY */
    if (access & ZX_GENERIC_WRITE) flags = 2; /* O_RDWR */
    if (creation_disposition == ZX_CREATE_ALWAYS || creation_disposition == ZX_CREATE_NEW) {
        flags |= 0x40; /* O_CREAT */
    }

    int res = syscall2(SYS_OPEN, (uint32_t)filename, flags);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HANDLE)res;
}

BOOL ReadFile(HANDLE file, void* buffer, DWORD bytes_to_read, DWORD* bytes_read_out) {
    int res = syscall3(SYS_READ, (uint32_t)file, (uint32_t)buffer, bytes_to_read);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_read_out) *bytes_read_out = (DWORD)res;
    return TRUE;
}

BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytes_to_write, DWORD* bytes_written_out) {
    int res = syscall3(SYS_WRITE, (uint32_t)file, (uint32_t)buffer, bytes_to_write);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_written_out) *bytes_written_out = (DWORD)res;
    return TRUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, DWORD move_method) {
    /* Stub: LSEEK syscall'a baglanacak */
    return 0;
}

BOOL DeleteFile(LPCSTR filename) {
    /* Stub: UNLINK syscall'a baglanacak */
    return FALSE;
}

/* Cihaz */
HANDLE OpenDevice(LPCSTR device_name) {
    /* Sürücü yollarina özel koruma ("vfs_open" altinda /dev/xxx). */
    return CreateFile(device_name, ZX_GENERIC_READ | ZX_GENERIC_WRITE, ZX_OPEN_EXISTING);
}

BOOL DeviceIoControl(HANDLE device, DWORD control_code, void* in_buffer, DWORD in_size, void* out_buffer, DWORD out_size, DWORD* bytes_returned) {
    /* Kernel'e struct pointer atilacak, 3 arguman yetmedigi icin user space'te struct paketlenir */
    uint32_t args[4] = { (uint32_t)in_buffer, in_size, (uint32_t)out_buffer, out_size };
    int res = syscall3(SYS_DEVICE_EXT, (uint32_t)device, control_code, (uint32_t)&args);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    if (bytes_returned) *bytes_returned = (DWORD)res;
    return TRUE;
}

BOOL CloseDevice(HANDLE device) {
    return CloseHandle(device);
}
