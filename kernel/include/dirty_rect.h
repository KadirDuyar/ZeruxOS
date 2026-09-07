/* =============================================================================
 * ZeruX OS — Dirty Rectangle Manager (Graphics Subsystem, Stage 1)
 * File: kernel/include/dirty_rect.h
 * =============================================================================
 * Instead of re-copying the entire screen (1024x768x4 = 3MB) to VRAM on every
 * redraw, drawing code calls dirty_mark() to record which small region it
 * just touched. The framebuffer manager (fb_present()) then copies ONLY
 * those regions to VRAM. This is the technique every real display stack
 * (X11, Windows GDI, Cocoa) uses, and it is what the roadmap calls out as
 * "Windows XP bile bunu kullaniyordu."
 * =============================================================================
 */

#ifndef DIRTY_RECT_H
#define DIRTY_RECT_H

#include <stdint.h>
#include <stdbool.h>

/* If more than this many disjoint rects accumulate in one frame, the
 * manager gives up merging them individually and just marks the whole
 * screen dirty (still correct, just less optimal for that one frame). */
#define DIRTY_MAX_RECTS 64

typedef struct {
    int32_t  x, y;
    int32_t  w, h;
} dirty_rect_t;

/* Must be called once, after the screen resolution is known (after vbe_init()). */
void dirty_init(uint32_t screen_width, uint32_t screen_height);

/* Records that the rectangle [x, x+w) x [y, y+h) changed. Automatically
 * clips to screen bounds and merges with an existing rect when it's cheap
 * to do so (overlapping / adjacent rects get unioned). */
void dirty_mark(int32_t x, int32_t y, int32_t w, int32_t h);

/* Marks the entire screen dirty (e.g. after a mode change or full redraw). */
void dirty_mark_all(void);

/* True if dirty_mark_all() (or overflow) means "just flush everything". */
bool dirty_is_full_screen(void);

/* Number of individual rects currently queued (meaningless if full-screen). */
uint32_t dirty_count(void);

/* Fetch rect 'index' (0 <= index < dirty_count()). Returns false if out of range. */
bool dirty_get(uint32_t index, dirty_rect_t *out);

/* Clears the dirty list. Call after fb_present() has flushed everything. */
void dirty_reset(void);

#endif /* DIRTY_RECT_H */
