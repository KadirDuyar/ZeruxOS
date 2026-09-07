/* =============================================================================
 * ZeruX OS — Compositor (Paint Layer)
 * File: kernel/gui/compositor.c
 * =============================================================================
 * Damage-tracked painter's algorithm: instead of repainting the entire
 * desktop every call (the Stage-1 version), this tracks which windows
 * moved, changed content (wm_invalidate()), or disappeared since the last
 * compose(), and:
 *
 *   1. If NOTHING changed since last frame -> does nothing at all. No fill,
 *      no blit, zero pixels touched, zero dirty_mark() calls.
 *   2. If something DID change -> computes the bounding rect of every
 *      changed window's old position UNION new position (so both where it
 *      used to be and where it is now get erased/redrawn), clips the
 *      screen surface to that single bounding rect (surface_set_clip,
 *      from Stage 1's clip-rect work), and repaints only the windows that
 *      overlap it -- in z-order, so occlusion still comes out correct.
 *
 * This is a single-rect damage tracker, not a full multi-region one: two
 * windows moving in opposite corners of the screen in the same frame will
 * still produce one big bounding rect covering both, even though the
 * pixels between them didn't change. That's a deliberate simplification --
 * it's a large, easy-to-verify win over "redraw everything, every frame"
 * with a fraction of the complexity of true multi-region damage lists.
 * Revisit if you get to a window count/layout where that gap starts to
 * matter in practice.
 * =============================================================================
 */

#include "compositor.h"
#include "window.h"
#include "framebuffer.h"
#include "dirty_rect.h"
#include "gfx2d.h"
#include "png.h"
#include "desktop_ui.h"
#include "rtc.h"
#include <stddef.h>
#include <stdbool.h>

#define COMPOSITOR_DESKTOP_BG     0x1E1E2Eu
#define COMPOSITOR_TITLE_FOCUSED  0x0A5FBFu
#define COMPOSITOR_TITLE_BLURRED  0x707070u
#define COMPOSITOR_BORDER_COLOR   0x202020u
#define COMPOSITOR_CLOSE_BG       0xC0392Bu
#define COMPOSITOR_CLOSE_X        0xFFFFFFu
#define COMPOSITOR_TITLE_TEXT     0xFFFFFFu

static void paint_window(gfx_surface_t *screen, window_t *win) {
    if (!win->visible) return;

    uint32_t titlebar_color = win->focused ? COMPOSITOR_TITLE_FOCUSED : COMPOSITOR_TITLE_BLURRED;

    /* Title bar */
    gfx_fill_rect(screen, win->x, win->y, win->w, WM_TITLEBAR_H, titlebar_color);
    gfx_draw_text(screen, win->x + 6, win->y + 4, win->title, COMPOSITOR_TITLE_TEXT, titlebar_color);

    /* Close button (top-right corner square of the title bar) */
    int32_t close_x = win->x + win->w - WM_TITLEBAR_H;
    gfx_fill_rect(screen, close_x, win->y, WM_TITLEBAR_H, WM_TITLEBAR_H, COMPOSITOR_CLOSE_BG);
    gfx_draw_line(screen, close_x + 6, win->y + 6, close_x + WM_TITLEBAR_H - 6, win->y + WM_TITLEBAR_H - 6, COMPOSITOR_CLOSE_X);
    gfx_draw_line(screen, close_x + WM_TITLEBAR_H - 6, win->y + 6, close_x + 6, win->y + WM_TITLEBAR_H - 6, COMPOSITOR_CLOSE_X);

    /* Maximize button */
    int32_t max_x = close_x - WM_TITLEBAR_H;
    gfx_fill_rect(screen, max_x, win->y, WM_TITLEBAR_H, WM_TITLEBAR_H, 0x4A4A4A);
    gfx_draw_rect(screen, max_x + 4, win->y + 4, WM_TITLEBAR_H - 8, WM_TITLEBAR_H - 8, 0xFFFFFF);

    /* Client-area content, blitted straight from the window's own surface */
    if (win->content && (uint32_t)win->content > 0x100000 && (uint32_t)win->content < 0x80000000 && win->content->pixels) {
        gfx_blit(screen, win->x, win->y + WM_TITLEBAR_H,
                  win->content, 0, 0,
                  (int32_t)win->content->width, (int32_t)win->content->height);
        win->content->is_dirty = false;
    }

    /* Border around the whole window (chrome included) */
    gfx_draw_rect(screen, win->x, win->y, win->w, win->h, COMPOSITOR_BORDER_COLOR);

    win->dirty = false;
}

/* ── Damage tracking state ────────────────────────────────────────────
 * One snapshot slot per window_t slot (window.h's ids ARE slot indices,
 * see window.c), so this stays trivially in sync with wm_create_window()/
 * wm_destroy_window() reusing slots -- no extra bookkeeping needed there. */
typedef struct {
    int32_t x, y, w, h;
} wm_snapshot_t;

static wm_snapshot_t g_prev[WM_MAX_WINDOWS];
static bool           g_have_prev[WM_MAX_WINDOWS];
static bool           g_first_frame = true;

static gfx_surface_t *g_desktop_bg = NULL;
static bool g_bg_loaded = false;

static void extend_damage(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1, bool *has,
                           int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    int32_t rx1 = x + w, ry1 = y + h;
    if (!*has) { *x0 = x; *y0 = y; *x1 = rx1; *y1 = ry1; *has = true; return; }
    if (x   < *x0) *x0 = x;
    if (y   < *y0) *y0 = y;
    if (rx1 > *x1) *x1 = rx1;
    if (ry1 > *y1) *y1 = ry1;
}

void compositor_compose(void) {
    gfx_surface_t *screen = fb_get_screen_surface();
    if (!screen) return; /* fb_init() hasn't run yet */

    int32_t dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
    bool has_damage = false;

    if (g_first_frame) {
        desktop_ui_init();
        dx0 = 0; dy0 = 0;
        dx1 = (int32_t)screen->width;
        dy1 = (int32_t)screen->height;
        has_damage = true;
        g_first_frame = false;
    }

    /* Redraw taskbar whenever RTC second ticks or network IP changes */
    static uint32_t s_last_sec = 0xFFFFFFFF;
    static uint32_t s_last_ip  = 0xFFFFFFFF;
    extern uint32_t g_local_ip;
    rtc_time_t rtc;
    rtc_read_time(&rtc);
    if ((uint32_t)rtc.second != s_last_sec || g_local_ip != s_last_ip) {
        s_last_sec = (uint32_t)rtc.second;
        s_last_ip  = g_local_ip;
        extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, 0, 0, (int32_t)screen->width, TASKBAR_HEIGHT);
    }

    uint32_t count = wm_get_zorder_count();
    bool seen_this_frame[WM_MAX_WINDOWS] = { 0 };

    /* Pass 1: every window currently on screen that moved, resized, or
     * was flagged dirty (wm_invalidate(), or freshly created -- see
     * wm_create_window(), which sets win->dirty = true) damages the
     * union of where it WAS and where it IS now. */
    for (uint32_t i = 0; i < count; i++) {
        window_t *win = wm_get_window_by_zorder(i);
        if (!win) continue;
        seen_this_frame[win->id] = true;

        if (g_have_prev[win->id]) {
            wm_snapshot_t *p = &g_prev[win->id];
            bool bounds_changed = (p->x != win->x || p->y != win->y || p->w != win->w || p->h != win->h);
            if (bounds_changed || win->dirty) {
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, p->x, p->y, p->w, p->h);
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, win->x, win->y, win->w, win->h);
            } else if (win->content && (uint32_t)win->content > 0x100000 && (uint32_t)win->content < 0x80000000 && win->content->is_dirty) {
                extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage,
                              win->x + win->content->dirty_x0,
                              win->y + WM_TITLEBAR_H + win->content->dirty_y0,
                              win->content->dirty_x1 - win->content->dirty_x0,
                              win->content->dirty_y1 - win->content->dirty_y0);
            }
        } else {
            extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, win->x, win->y, win->w, win->h);
        }
    }

    /* Pass 2: a slot that had a window last frame but doesn't now -- its
     * old footprint needs the desktop background redrawn over it. */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (g_have_prev[i] && !seen_this_frame[i]) {
            extend_damage(&dx0, &dy0, &dx1, &dy1, &has_damage, g_prev[i].x, g_prev[i].y, g_prev[i].w, g_prev[i].h);
            g_have_prev[i] = false;
        }
    }

    if (!has_damage) return; /* nothing changed since last frame -- do nothing at all */

    surface_set_clip(screen, dx0, dy0, dx1 - dx0, dy1 - dy0);
    
    if (!g_bg_loaded) {
        g_bg_loaded = true;
        g_desktop_bg = png_load("/disk/fat0/WALLPAPR.PNG");
    }

    gfx_fill_rect(screen, dx0, dy0, dx1 - dx0, dy1 - dy0, COMPOSITOR_DESKTOP_BG);

    if (g_desktop_bg && (uint32_t)g_desktop_bg > 0x100000 && (uint32_t)g_desktop_bg < 0x80000000 && g_desktop_bg->pixels) {
        int32_t bg_dx = ((int32_t)screen->width - (int32_t)g_desktop_bg->width) / 2;
        int32_t bg_dy = ((int32_t)screen->height - (int32_t)g_desktop_bg->height) / 2;
        
        /* Only blit the portion of the background that overlaps with the dirty rect */
        /* gfx_blit automatically respects surface_set_clip, so we can just blit normally! */
        gfx_blit(screen, bg_dx, bg_dy, g_desktop_bg, 0, 0, g_desktop_bg->width, g_desktop_bg->height);
    }

    desktop_ui_draw_bg(screen, dx0, dy0, dx1, dy1);

    for (uint32_t i = 0; i < count; i++) { /* back (0) -> front (count-1): occlusion stays correct */
        window_t *win = wm_get_window_by_zorder(i);
        if (!win) continue;

        bool overlaps_damage = !(win->x + win->w <= dx0 || win->x >= dx1 ||
                                  win->y + win->h <= dy0 || win->y >= dy1);
        if (overlaps_damage) paint_window(screen, win);

        g_prev[win->id].x = win->x; g_prev[win->id].y = win->y;
        g_prev[win->id].w = win->w; g_prev[win->id].h = win->h;
        g_have_prev[win->id] = true;
    }

    desktop_ui_draw_fg(screen, dx0, dy0, dx1, dy1);

    surface_reset_clip(screen);

    if (has_damage) {
        dirty_mark(dx0, dy0, dx1 - dx0, dy1 - dy0);
    }
}
