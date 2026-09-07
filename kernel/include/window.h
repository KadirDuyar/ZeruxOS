/* =============================================================================
 * ZeruX OS — Window Manager (Data Model / Logic Layer)
 * File: kernel/include/window.h
 * =============================================================================
 * This is the "model" half of the Window Manager: window geometry, z-order,
 * focus, and mouse hit-testing (drag-to-move, click-to-focus, close button).
 * It owns no pixels beyond each window's own content surface.
 *
 * The "view" half — actually painting title bars and blitting windows to
 * the screen — lives in compositor.h/.c, which is built entirely on top
 * of this file's public API plus gfx2d.h/framebuffer.h. Keeping the two
 * separate mirrors how every real windowing system is structured (X11's
 * window tree vs. its compositor; Win32's USER vs. DWM).
 *
 * Each window owns an independent gfx_surface_t as its client-area
 * backing store. Applications draw into that surface with gfx2d.h calls
 * exactly like they would draw to the screen — and because it's a small,
 * separate surface, there is no way to accidentally paint outside your
 * own window. That's the entire point of the Surface abstraction from
 * Graphics Stage 1 paying off here.
 * =============================================================================
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include "surface.h"

#define WM_MAX_WINDOWS   16
#define WM_TITLEBAR_H    24   /* px */
#define WM_BORDER_W      1    /* px, drawn by the compositor around each window */
#define WM_TITLE_MAXLEN  64

typedef struct window window_t; /* forward declare so callback typedefs can reference it */

typedef void (*wm_key_handler_t)(window_t *win, char c);
typedef void (*wm_click_handler_t)(window_t *win, int32_t local_x, int32_t local_y);
typedef void (*wm_destroy_handler_t)(window_t *win);
typedef void (*wm_update_handler_t)(window_t *win); /* called once per frame */
typedef void (*wm_resize_handler_t)(window_t *win);

struct window {
    int32_t  id;              /* == its own slot index; -1 if this slot is free */
    int32_t  x, y;             /* top-left of the WHOLE window (chrome included) */
    int32_t  w, h;              /* WHOLE window size (chrome included)          */
    char     title[WM_TITLE_MAXLEN];
    gfx_surface_t *content;     /* client area: w x (h - WM_TITLEBAR_H)          */
    bool     visible;
    bool     focused;
    bool     dirty;             /* sticky: needs to be included in next compose */
    bool     maximized;
    int32_t  orig_x, orig_y, orig_w, orig_h; /* State before maximize */

    void *app_data;             /* app-owned state (e.g. terminal_state_t*)      */
    wm_key_handler_t     on_key;     /* NULL = window ignores keyboard input     */
    wm_click_handler_t   on_click;   /* NULL = window ignores content clicks     */
    wm_destroy_handler_t on_destroy; /* NULL = nothing to free besides content   */
    wm_update_handler_t  on_update;  /* NULL = no per-frame work                 */
    wm_resize_handler_t  on_resize;  /* NULL = ignores resize events             */
};

void wm_init(void);

/* Creates a window (allocates its content surface) and returns its id
 * (>= 0), or -1 on failure (table full / out of memory). New windows are
 * placed on top of the z-order and given focus. */
int32_t wm_create_window(const char *title, int32_t x, int32_t y, int32_t w, int32_t h);
int32_t wm_find_window_by_title(const char *title);
void wm_destroy_window(int32_t id);

/* NULL if id is invalid or the window was destroyed. */
gfx_surface_t *wm_get_content_surface(int32_t id);

/* Direct pointer to the window_t for a given id, or NULL. Prefer this over
 * wm_get_window_by_zorder() when you already have the id (e.g. right after
 * wm_create_window() returns it) — it's O(1) and doesn't depend on the
 * window still being on top of the z-order. */
window_t *wm_get_window(int32_t id);

/* Call after drawing new content into a window's surface so the
 * compositor picks it up on the next compositor_compose(). */
void wm_invalidate(int32_t id);

void wm_toggle_maximize(int32_t id);

void wm_move_window(int32_t id, int32_t new_x, int32_t new_y);
void wm_focus_window(int32_t id);           /* raises to front of z-order        */
void wm_show_window(int32_t id, bool visible);

/* Application hooks — set these right after wm_create_window() to build a
 * real app (see terminal_app.h / explorer_app.h for examples):
 *   on_key      — called for each keystroke while this window has focus
 *   on_click    — called with CONTENT-relative coords on a fresh left click
 *                 inside the client area (titlebar/close are handled by the
 *                 WM itself and never reach this callback)
 *   on_destroy  — called right before wm_destroy_window() frees the content
 *                 surface; free your app_data here to avoid leaking it
 *   on_resize   — called when the window is maximized or resized
 */
void  wm_set_app_data(int32_t id, void *data);
void *wm_get_app_data(int32_t id);
void  wm_set_key_handler(int32_t id, wm_key_handler_t handler);
void  wm_set_click_handler(int32_t id, wm_click_handler_t handler);
void  wm_set_destroy_handler(int32_t id, wm_destroy_handler_t handler);
void  wm_set_update_handler(int32_t id, wm_update_handler_t handler);
void  wm_set_resize_handler(int32_t id, wm_resize_handler_t handler);

int32_t wm_get_focused(void);

/* Direct access to a window's full record by id (not just its content
 * surface) — apps that keep their own per-window state (e.g. a File
 * Explorer's current path) use this to read live geometry/focus/visible
 * without duplicating that bookkeeping themselves. NULL if id is invalid
 * or the window has been destroyed. */
window_t *wm_get_window(int32_t id);

/* Topmost visible window whose whole-window rect contains (x,y), or -1. */
int32_t wm_window_at(int32_t x, int32_t y);

/* Direct read access for the compositor: number of live z-order slots and
 * the window at z-order position i (0 = bottom/back, count-1 = top/front). */
uint32_t   wm_get_zorder_count(void);
window_t  *wm_get_window_by_zorder(uint32_t index);

/* Reads mouse_x / mouse_y / mouse_left_btn (mouse.h) and the keyboard
 * queue (keyboard.h) once per call. Handles click-to-focus, drag-by-
 * titlebar, the close button, dispatches content clicks to the clicked
 * window's on_click, and dispatches every pending keystroke to the
 * FOCUSED window's on_key. Call once per frame, before compositor_compose(). */
void wm_update(void);

/* Optional smoke test: opens two overlapping windows with different
 * background colors and some text drawn into their content surfaces, so
 * you can immediately see z-order, chrome, and drag-to-move working end
 * to end. Not part of the "real" API — call it once after wm_init() while
 * bringing this subsystem up, then delete the call once you're building
 * real windows. */
void wm_spawn_demo_windows(void);

#endif /* WINDOW_H */
