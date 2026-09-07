/* =============================================================================
 * ZeruX OS — Window Manager (Data Model / Logic Layer)
 * File: kernel/gui/window.c
 * =============================================================================
 */

#include "window.h"
#include "desktop_ui.h"
#include "mouse.h"
#include "keyboard.h"
#include "serial.h"
#include "gfx2d.h"
#include "framebuffer.h"
#include "desktop_ui.h"
#include <stddef.h>

static window_t g_windows[WM_MAX_WINDOWS];
static int32_t  g_zorder[WM_MAX_WINDOWS]; /* slot indices, index 0 = bottom/back */
static uint32_t g_zcount = 0;

/* Small local helper — the codebase doesn't share a common string.h, each
 * driver rolls what it needs (see shell.c's kstrlen()), so we do the same. */
static void copy_title(char *dst, const char *src, size_t maxlen) {
    size_t i = 0;
    if (src) {
        for (; i < maxlen - 1 && src[i]; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        g_windows[i].id = -1;
        g_windows[i].content = NULL;
        g_windows[i].visible = false;
        g_windows[i].focused = false;
        g_windows[i].app_data = NULL;
        g_windows[i].on_key = NULL;
        g_windows[i].on_click = NULL;
        g_windows[i].on_destroy = NULL;
        g_windows[i].on_update = NULL;
        g_windows[i].on_resize = NULL;
    }
    g_zcount = 0;
    serial_printf("[WM] Window Manager initialized (max %d windows).\n", WM_MAX_WINDOWS);
}

static void zorder_remove(int32_t id) {
    for (uint32_t i = 0; i < g_zcount; i++) {
        if (g_zorder[i] == id) {
            for (uint32_t j = i; j + 1 < g_zcount; j++) g_zorder[j] = g_zorder[j + 1];
            g_zcount--;
            return;
        }
    }
}

static void zorder_raise(int32_t id) {
    zorder_remove(id);
    if (g_zcount < WM_MAX_WINDOWS) g_zorder[g_zcount++] = id;
}

int32_t wm_create_window(const char *title, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= WM_TITLEBAR_H) return -1;

    int32_t slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_windows[i].id == -1) { slot = i; break; }
    }
    if (slot == -1) {
        serial_printf("[WM] wm_create_window: table full.\n");
        return -1;
    }

    gfx_surface_t *content = surface_create((uint32_t)w, (uint32_t)(h - WM_TITLEBAR_H));
    if (!content) {
        serial_printf("[WM] wm_create_window: out of memory for content surface.\n");
        return -1;
    }
    surface_clear(content, 0xFFFFFF);

    window_t *win = &g_windows[slot];
    win->id      = slot;
    win->x       = x;
    win->y       = y;
    win->w       = w;
    win->h       = h;
    win->content = content;
    win->visible = true;
    win->focused = false;
    win->dirty   = true;
    win->app_data   = NULL;
    win->on_key     = NULL;
    win->on_click   = NULL;
    win->on_destroy = NULL;
    win->on_update  = NULL;
    win->on_resize  = NULL;
    copy_title(win->title, title, WM_TITLE_MAXLEN);

    for (uint32_t i = 0; i < g_zcount; i++) g_windows[g_zorder[i]].focused = false;
    zorder_raise(slot);
    win->focused = true;
    extern void dirty_mark_all(void);
    dirty_mark_all();

    serial_printf("[WM] Created window '%s' id=%d at (%d,%d) %dx%d\n", win->title, slot, x, y, w, h);
    return slot;
}

int32_t wm_find_window_by_title(const char *title) {
    if (!title) return -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_windows[i].id != -1) {
            /* Simple strcmp since we don't have standard string.h */
            const char *a = g_windows[i].title;
            const char *b = title;
            bool match = true;
            while (*a || *b) {
                if (*a != *b) { match = false; break; }
                a++; b++;
            }
            if (match) return g_windows[i].id;
        }
    }
    return -1;
}

void wm_destroy_window(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;

    window_t *win = &g_windows[id];
    if (win->on_destroy) win->on_destroy(win); /* let the app free its app_data first */
    if (win->content) surface_destroy(win->content);
    win->content   = NULL;
    win->id        = -1;
    win->visible   = false;
    win->focused   = false;
    win->app_data  = NULL;
    win->on_key    = NULL;
    win->on_click  = NULL;
    win->on_destroy = NULL;
    win->on_update  = NULL;
    win->on_resize  = NULL;

    zorder_remove(id);

    /* Give focus to whatever is now on top, if anything */
    if (g_zcount > 0) g_windows[g_zorder[g_zcount - 1]].focused = true;
    
    extern void dirty_mark_all(void);
    dirty_mark_all();
}

void wm_toggle_maximize(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    window_t *win = &g_windows[id];

    gfx_surface_t *screen = fb_get_screen_surface();
    if (!screen) return;

    if (!win->maximized) {
        /* Maximize et: mevcut durumu kaydet */
        win->orig_x = win->x;
        win->orig_y = win->y;
        win->orig_w = win->w;
        win->orig_h = win->h;

        win->x = 0;
        win->y = TASKBAR_HEIGHT;
        win->w = (int32_t)screen->width;
        win->h = (int32_t)screen->height - TASKBAR_HEIGHT;
        win->maximized = true;
    } else {
        /* Geri yukle */
        win->x = win->orig_x;
        win->y = win->orig_y;
        win->w = win->orig_w;
        win->h = win->orig_h;
        win->maximized = false;
    }

    /* Icerik yuzeyini yeni boyuta gore yeniden olustur */
    gfx_surface_t *new_content = surface_create((uint32_t)win->w, (uint32_t)(win->h - WM_TITLEBAR_H));
    if (new_content) {
        surface_clear(new_content, 0xFFFFFF);
        if (win->content) surface_destroy(win->content);
        win->content = new_content;
    }
    
    win->dirty = true;
    if (win->on_resize) win->on_resize(win);
    wm_invalidate(id);
}

gfx_surface_t *wm_get_content_surface(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return NULL;
    return g_windows[id].content;
}

window_t *wm_get_window(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return NULL;
    return &g_windows[id];
}

void wm_invalidate(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].dirty = true;
}

void wm_move_window(int32_t id, int32_t new_x, int32_t new_y) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].x = new_x;
    g_windows[id].y = new_y;
    g_windows[id].dirty = true;
}

void wm_focus_window(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    for (uint32_t i = 0; i < g_zcount; i++) g_windows[g_zorder[i]].focused = false;
    zorder_raise(id);
    g_windows[id].focused = true;
    extern void dirty_mark_all(void);
    dirty_mark_all();
}

void wm_show_window(int32_t id, bool visible) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].visible = visible;
    g_windows[id].dirty = true;
}

void wm_set_app_data(int32_t id, void *data) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].app_data = data;
}

void *wm_get_app_data(int32_t id) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return NULL;
    return g_windows[id].app_data;
}

void wm_set_key_handler(int32_t id, wm_key_handler_t handler) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].on_key = handler;
}

void wm_set_click_handler(int32_t id, wm_click_handler_t handler) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].on_click = handler;
}

void wm_set_destroy_handler(int32_t id, wm_destroy_handler_t handler) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].on_destroy = handler;
}

void wm_set_update_handler(int32_t id, wm_update_handler_t handler) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].on_update = handler;
}

void wm_set_resize_handler(int32_t id, wm_resize_handler_t handler) {
    if (id < 0 || id >= WM_MAX_WINDOWS || g_windows[id].id == -1) return;
    g_windows[id].on_resize = handler;
}

int32_t wm_get_focused(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_windows[i].id != -1 && g_windows[i].focused) return i;
    }
    return -1;
}

int32_t wm_window_at(int32_t x, int32_t y) {
    for (int32_t i = (int32_t)g_zcount - 1; i >= 0; i--) {
        window_t *win = &g_windows[g_zorder[i]];
        if (!win->visible) continue;
        if (x >= win->x && x < win->x + win->w && y >= win->y && y < win->y + win->h) {
            return win->id;
        }
    }
    return -1;
}

uint32_t wm_get_zorder_count(void) {
    return g_zcount;
}

window_t *wm_get_window_by_zorder(uint32_t index) {
    if (index >= g_zcount) return NULL;
    return &g_windows[g_zorder[index]];
}

/* =============================================================================
 * Mouse interaction: click-to-focus, drag-by-titlebar, close button
 * =============================================================================
 */
static int32_t g_dragging_id = -1;
static int32_t g_drag_off_x  = 0;
static int32_t g_drag_off_y  = 0;
static bool    g_prev_left   = false;

void wm_update(void) {
    desktop_ui_update();
    
    bool left = mouse_left_btn;

    if (left && !g_prev_left) {
        if (desktop_ui_handle_click(mouse_x, mouse_y)) {
            g_dragging_id = -1;
        } else {
            /* Fresh press this frame — hit-test */
            int32_t hit = wm_window_at(mouse_x, mouse_y);
            if (hit >= 0) {
                window_t *win = &g_windows[hit];

            int32_t close_x0 = win->x + win->w - WM_TITLEBAR_H;
            int32_t max_x0   = close_x0 - WM_TITLEBAR_H;
            bool on_titlebar = (mouse_y >= win->y && mouse_y < win->y + WM_TITLEBAR_H);
            bool on_close    = on_titlebar && mouse_x >= close_x0 && mouse_x < win->x + win->w;
            bool on_max      = on_titlebar && mouse_x >= max_x0 && mouse_x < close_x0;

            if (on_close) {
                wm_destroy_window(hit);
                g_dragging_id = -1;
            } else if (on_max) {
                wm_focus_window(hit);
                wm_toggle_maximize(hit);
                g_dragging_id = -1;
            } else {
                wm_focus_window(hit);
                if (on_titlebar) {
                    g_dragging_id = hit;
                    g_drag_off_x  = mouse_x - win->x;
                    g_drag_off_y  = mouse_y - win->y;
                } else if (win->on_click) {
                    /* Click landed in the content area — report window-local coords */
                    int32_t local_x = mouse_x - win->x;
                    int32_t local_y = mouse_y - win->y - WM_TITLEBAR_H;
                    win->on_click(win, local_x, local_y);
                }
            }
            }
        }
    } else if (left && g_dragging_id >= 0) {
        wm_move_window(g_dragging_id, mouse_x - g_drag_off_x, mouse_y - g_drag_off_y);
    } else if (!left) {
        g_dragging_id = -1;
    }

    g_prev_left = left;

    /* Route every pending keystroke to the currently focused window */
    int32_t focused = wm_get_focused();
    if (focused >= 0 && g_windows[focused].on_key) {
        while (keyboard_has_char()) {
            char c = keyboard_getchar();
            g_windows[focused].on_key(&g_windows[focused], c);
        }
    } else {
        /* No focused window wants keys right now — drain the queue anyway
         * so stale input doesn't leak into whichever window gains focus next */
        while (keyboard_has_char()) keyboard_getchar();
    }

    /* Per-frame per-window update callbacks (e.g. browser checks redraw flag) */
    for (uint32_t i = 0; i < g_zcount; i++) {
        window_t *w = &g_windows[g_zorder[i]];
        if (w->id >= 0 && w->on_update) w->on_update(w);
    }
}

/* =============================================================================
 * Optional smoke test — see window.h
 * =============================================================================
 */
void wm_spawn_demo_windows(void) {
    int32_t a = wm_create_window("Demo A", 120, 100, 320, 220);
    int32_t b = wm_create_window("Demo B", 260, 180, 320, 220);

    gfx_surface_t *ca = wm_get_content_surface(a);
    if (ca) {
        surface_clear(ca, 0xFFF3CD);
        gfx_draw_text(ca, 12, 12, "Pencere A", 0x000000, 0xFFF3CD);
        gfx_draw_text(ca, 12, 32, "Baslik cubugundan surukle", 0x000000, 0xFFF3CD);
    }

    gfx_surface_t *cb = wm_get_content_surface(b);
    if (cb) {
        surface_clear(cb, 0xD1E7DDu);
        gfx_draw_text(cb, 12, 12, "Pencere B", 0x000000, 0xD1E7DDu);
        gfx_draw_text(cb, 12, 32, "Ustune tikla, one gelsin", 0x000000, 0xD1E7DDu);
    }

    serial_printf("[WM] Demo windows spawned (ids %d, %d).\n", a, b);
}
