#ifndef _ZX_MSG_H
#define _ZX_MSG_H

#include "../core/zx_types.h"

/* Temel Pencere Mesajlari */
#define ZX_MSG_NULL         0x0000
#define ZX_MSG_CREATE       0x0001
#define ZX_MSG_DESTROY      0x0002
#define ZX_MSG_PAINT        0x000F
#define ZX_MSG_QUIT         0x0012

#define ZX_MSG_KEYDOWN      0x0100
#define ZX_MSG_KEYUP        0x0101

#define ZX_MSG_MOUSEMOVE    0x0200
#define ZX_MSG_LBUTTONDOWN  0x0201
#define ZX_MSG_LBUTTONUP    0x0202
#define ZX_MSG_RBUTTONDOWN  0x0204
#define ZX_MSG_RBUTTONUP    0x0205

typedef struct {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
} ZX_MSG;

BOOL GetMessage(ZX_MSG *msg, HWND hwnd);
BOOL PeekMessage(ZX_MSG *msg, HWND hwnd, UINT msg_filter_min, UINT msg_filter_max, UINT remove_msg);
BOOL PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LPARAM DispatchMessage(const ZX_MSG *msg);

#endif /* _ZX_MSG_H */
