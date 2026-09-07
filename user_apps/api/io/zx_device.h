#ifndef _ZX_DEVICE_H
#define _ZX_DEVICE_H

#include "../core/zx_types.h"

/* I/O Kontrol Kodlari (IOCTL) */
#define ZX_IOCTL_GET_INFO      0x01
#define ZX_IOCTL_SET_MODE      0x02
#define ZX_IOCTL_RESET         0x03

/* Cihaz API */
HANDLE OpenDevice(LPCSTR device_name);
BOOL   DeviceIoControl(HANDLE device, DWORD control_code, void* in_buffer, DWORD in_size, void* out_buffer, DWORD out_size, DWORD* bytes_returned);
BOOL   CloseDevice(HANDLE device);

#endif /* _ZX_DEVICE_H */
