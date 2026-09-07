/* =============================================================================
 * ZeruX OS — Surface Abstraction (Graphics Subsystem, Stage 1)
 * File: kernel/include/surface.h
 * =============================================================================
 * A "surface" is just a rectangle of 32bpp pixels (0x00RRGGBB) plus its
 * dimensions. Every drawing primitive in gfx2d.h operates on a gfx_surface_t*
 * instead of writing to the global VBE shadow buffer directly.
 *
 * This is what lets the SAME gfx_draw_* / gfx_fill_* code later draw into:
 *   - the screen back-buffer (via fb_get_screen_surface())
 *   - an off-screen window buffer (once you build the Window Manager)
 *   - a sprite / icon loaded from disk
 * without any of that code needing to change. It is the foundation the
 * roadmap calls the "Surface System".
 * =============================================================================
 */

#ifndef SURFACE_H
#define SURFACE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t *pixels;      /* row-major 32bpp pixel data, 0x00RRGGBB           */
    uint32_t  width;       /* visible width in pixels                          */
    uint32_t  height;      /* visible height in pixels                         */
    uint32_t  pitch;       /* pixels PER ROW in the backing buffer (>= width)  */
    bool      owns_memory; /* true => surface_destroy() must kfree(pixels)     */

    /* Clip rectangle — every gfx2d.h drawing call is additionally clipped
     * to this rect before touching a pixel. This is the same role GDI's
     * IntersectClipRect() plays on an HDC: it's what stops a window's
     * contents from being drawn outside its own borders once you build
     * a Window Manager on top of this. Defaults to the full surface. */
    int32_t   clip_x, clip_y, clip_w, clip_h;

    /* Auto-diff dirty rectangle tracking */
    bool      is_dirty;
    int32_t   dirty_x0, dirty_y0, dirty_x1, dirty_y1;
} gfx_surface_t;

/* Allocates a new, owned, off-screen surface (pitch == width). Zero-filled. */
gfx_surface_t *surface_create(uint32_t width, uint32_t height);

/* Frees a surface created with surface_create(). Safe to call with NULL. */
void surface_destroy(gfx_surface_t *surf);

/* Fills 'out' as a non-owning view over caller-supplied pixel memory
 * (e.g. the VBE shadow buffer). surface_destroy() on this is a no-op
 * for the pixel data (owns_memory = false). */
void surface_wrap(gfx_surface_t *out, uint32_t *pixels,
                   uint32_t width, uint32_t height, uint32_t pitch);

/* Fills the whole surface with a single color. */
void surface_clear(gfx_surface_t *surf, uint32_t color);

/* Resets the clip rectangle to cover the entire surface. Called
 * automatically by surface_create()/surface_wrap(); call it again
 * whenever you want to stop clipping (e.g. before drawing a new frame). */
void surface_reset_clip(gfx_surface_t *surf);

/* Intersects the current clip rectangle with [x, x+w) x [y, y+h), and
 * clamps the result to the surface bounds. Equivalent to GDI's
 * IntersectClipRect(). Nest calls to progressively narrow the clip
 * (e.g. child-window-inside-parent-window clipping). */
void surface_set_clip(gfx_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h);

#endif /* SURFACE_H */
