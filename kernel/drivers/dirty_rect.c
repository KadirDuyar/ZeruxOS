/* =============================================================================
 * ZeruX OS — Dirty Rectangle Manager (Graphics Subsystem, Stage 1)
 * File: kernel/drivers/dirty_rect.c
 * =============================================================================
 */

#include "dirty_rect.h"
#include <stddef.h>

static dirty_rect_t g_rects[DIRTY_MAX_RECTS];
static uint32_t     g_count       = 0;
static bool         g_full_screen = true; /* start dirty so the first frame always draws */
static int32_t      g_screen_w    = 0;
static int32_t      g_screen_h    = 0;

void dirty_init(uint32_t screen_width, uint32_t screen_height) {
    g_screen_w    = (int32_t)screen_width;
    g_screen_h    = (int32_t)screen_height;
    g_count       = 0;
    g_full_screen = true; /* force a full present on the very first frame */
}

void dirty_mark_all(void) {
    g_full_screen = true;
    g_count       = 0;
}

bool dirty_is_full_screen(void) {
    return g_full_screen;
}

static bool rects_touch(const dirty_rect_t *a, const dirty_rect_t *b) {
    /* True if the rectangles overlap or share an edge (cheap to merge) */
    return !(a->x > b->x + b->w || b->x > a->x + a->w ||
             a->y > b->y + b->h || b->y > a->y + a->h);
}

static void rects_union(dirty_rect_t *a, const dirty_rect_t *b) {
    int32_t x0 = (a->x < b->x) ? a->x : b->x;
    int32_t y0 = (a->y < b->y) ? a->y : b->y;
    int32_t x1 = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    int32_t y1 = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    a->x = x0; a->y = y0;
    a->w = x1 - x0; a->h = y1 - y0;
}

void dirty_mark(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (g_full_screen) return; /* already flushing everything this frame */
    if (w <= 0 || h <= 0) return;

    /* Clip to screen bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (g_screen_w > 0 && x + w > g_screen_w) w = g_screen_w - x;
    if (g_screen_h > 0 && y + h > g_screen_h) h = g_screen_h - y;
    if (w <= 0 || h <= 0) return;

    dirty_rect_t nr = { x, y, w, h };

    /* Try to merge into an existing rect first (keeps the list small) */
    for (uint32_t i = 0; i < g_count; i++) {
        if (rects_touch(&g_rects[i], &nr)) {
            rects_union(&g_rects[i], &nr);
            return;
        }
    }

    if (g_count >= DIRTY_MAX_RECTS) {
        /* Too fragmented to track individually — fall back to a full flush */
        dirty_mark_all();
        return;
    }

    g_rects[g_count++] = nr;
}

uint32_t dirty_count(void) {
    return g_full_screen ? 0 : g_count;
}

bool dirty_get(uint32_t index, dirty_rect_t *out) {
    if (g_full_screen || index >= g_count || !out) return false;
    *out = g_rects[index];
    return true;
}

void dirty_reset(void) {
    g_count       = 0;
    g_full_screen = false;
}
