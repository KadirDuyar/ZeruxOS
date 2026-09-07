#include "zx_window.h"
#include "zx_msg.h"
#include "zx_graphics.h"
#include "../core/zx_error.h"
#include "../core/zx_handle.h"

#define SYS_GUI 29

extern int syscall4(int sys_num, int arg1, int arg2, int arg3, int arg4);
extern int syscall3(int sys_num, int arg1, int arg2, int arg3);
extern int syscall2(int sys_num, int arg1, int arg2);
extern int syscall1(int sys_num, int arg1);

/* zx_window */
HWND CreateWindowEx(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width, int height, HWND parent, HANDLE menu, HANDLE instance, void* param) {
    ZX_CREATE_STRUCT cs;
    cs.exStyle = exStyle;
    cs.className = className;
    cs.windowName = windowName;
    cs.style = style;
    cs.x = x; cs.y = y; cs.width = width; cs.height = height;
    cs.parent = parent; cs.menu = menu; cs.instance = instance; cs.param = param;
    
    int res = syscall2(SYS_GUI, 0 /* GUI_CREATE_WINDOW */, (uint32_t)&cs);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HWND)res;
}

BOOL DestroyWindow(HWND hwnd) {
    /* Arka planda CloseHandle cagirilarak objenin yok edilmesi (ref_count) tetiklenir.
       Fakat GUI subsystem icinde wm_destroy calismasi icin ayri komut da olabilir. 
       Biz simdilik CloseHandle kullanacagiz. */
    return CloseHandle((HANDLE)hwnd);
}

BOOL ShowWindow(HWND hwnd, int command) {
    int res = syscall3(SYS_GUI, 2 /* GUI_SHOW_WINDOW */, (uint32_t)hwnd, command);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

/* zx_msg */
BOOL GetMessage(ZX_MSG *msg, HWND hwnd) {
    if (!msg) { SetLastError(ZX_ERROR_INVALID_PARAMETER); return FALSE; }
    int res = syscall3(SYS_GUI, 3 /* GUI_GET_MESSAGE */, (uint32_t)msg, (uint32_t)hwnd);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return (res > 0); /* 0 ise QUIT mesaji gelmis demektir */
}

BOOL PeekMessage(ZX_MSG *msg, HWND hwnd, UINT msg_filter_min, UINT msg_filter_max, UINT remove_msg) {
    /* TODO: Struct yapip 3'ten fazla argumani pakette yolla */
    int res = syscall3(SYS_GUI, 4 /* GUI_PEEK_MESSAGE */, (uint32_t)msg, (uint32_t)hwnd);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return (res > 0);
}

BOOL PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ZX_MSG m;
    m.hwnd = hwnd; m.message = msg; m.wParam = wParam; m.lParam = lParam; m.time = 0;
    int res = syscall2(SYS_GUI, 5 /* GUI_POST_MESSAGE */, (uint32_t)&m);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

LPARAM DispatchMessage(const ZX_MSG *msg) {
    /* Normalde WndProc'u (user space callback) cagirir. ZeruX'da su an pencere sistemimiz 
       kernel uzerinden handle edildiginden DispatchMessage'i kernel'e forwardlayabiliriz veya 
       ileride gercek WndProc destegi eklersek direk fonksiyon pointerini cagiririz. */
    int res = syscall2(SYS_GUI, 6 /* GUI_DISPATCH_MESSAGE */, (uint32_t)msg);
    return (LPARAM)res;
}

/* zx_graphics */
HDC BeginPaint(HWND hwnd, ZX_PAINTSTRUCT *ps) {
    int res = syscall3(SYS_GUI, 7 /* GUI_BEGIN_PAINT */, (uint32_t)hwnd, (uint32_t)ps);
    if (res < 0) { SetLastError((DWORD)(-res)); return NULL_HANDLE; }
    return (HDC)res;
}

BOOL EndPaint(HWND hwnd, const ZX_PAINTSTRUCT *ps) {
    int res = syscall3(SYS_GUI, 8 /* GUI_END_PAINT */, (uint32_t)hwnd, (uint32_t)ps);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

BOOL FillRect(HDC hdc, const ZX_RECT *rect, DWORD color) {
    int res = syscall4(SYS_GUI, 9 /* GUI_FILL_RECT */, (uint32_t)hdc, (uint32_t)rect, color);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}

BOOL DrawText(HDC hdc, LPCSTR text, int length, ZX_RECT *rect, DWORD format) {
    /* Argumanlari paketlememiz gerekecek ileride (3 limitini asiyoruz text, length, rect, format).
       Simdilik stub (kernel gormezden gelsin). */
    int res = syscall3(SYS_GUI, 10 /* GUI_DRAW_TEXT */, (uint32_t)hdc, (uint32_t)text);
    if (res < 0) { SetLastError((DWORD)(-res)); return FALSE; }
    return TRUE;
}
