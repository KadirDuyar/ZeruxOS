#ifndef _ZX_GRAPHICS_H
#define _ZX_GRAPHICS_H

#include "../core/zx_types.h"

typedef HANDLE HDC; /* Device Context (Cizim Yoneticisi) Handle */

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} ZX_RECT;

typedef struct {
    HDC     hdc;
    BOOL    erase;
    ZX_RECT paint_rect;
} ZX_PAINTSTRUCT;

HDC  BeginPaint(HWND hwnd, ZX_PAINTSTRUCT *ps);
BOOL EndPaint(HWND hwnd, const ZX_PAINTSTRUCT *ps);
BOOL FillRect(HDC hdc, const ZX_RECT *rect, DWORD color);
BOOL DrawText(HDC hdc, LPCSTR text, int length, ZX_RECT *rect, DWORD format);

#endif /* _ZX_GRAPHICS_H */
