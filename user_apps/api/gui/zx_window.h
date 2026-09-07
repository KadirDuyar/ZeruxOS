#ifndef _ZX_WINDOW_H
#define _ZX_WINDOW_H

#include "../core/zx_types.h"

#define ZX_WS_OVERLAPPEDWINDOW 0x00CF0000
#define ZX_WS_POPUP            0x80000000
#define ZX_WS_VISIBLE          0x10000000

#define ZX_SW_HIDE             0
#define ZX_SW_SHOW             5

/* Pencere Olusturma Icin Paket (12 Arguman sinirini asmak icin) */
typedef struct {
    DWORD  exStyle;
    LPCSTR className;
    LPCSTR windowName;
    DWORD  style;
    int    x;
    int    y;
    int    width;
    int    height;
    HWND   parent;
    HANDLE menu;
    HANDLE instance;
    void*  param;
} ZX_CREATE_STRUCT;

HWND CreateWindowEx(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width, int height, HWND parent, HANDLE menu, HANDLE instance, void* param);
BOOL DestroyWindow(HWND hwnd);
BOOL ShowWindow(HWND hwnd, int command);

#endif /* _ZX_WINDOW_H */
