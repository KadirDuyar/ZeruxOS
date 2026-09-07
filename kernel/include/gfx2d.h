/* =============================================================================
 * ZeruX OS — 2D Graphics Library (Graphics Subsystem, Stage 1)
 * File: kernel/include/gfx2d.h
 * =============================================================================
 * Every primitive here takes a gfx_surface_t* target, so the exact same
 * function draws to the visible screen, an off-screen window buffer, or a
 * loaded icon — whichever surface you pass in. This is the "2D Graphics
 * Engine" layer from the roadmap; the GUI/Window Manager should be built
 * on top of this instead of poking pixels directly.
 *
 * Colors are 0x00RRGGBB (top byte unused), matching the format already
 * used throughout vbe.c / gui.c.
 * =============================================================================
 */

#ifndef GFX2D_H
#define GFX2D_H

#include <stdint.h>
#include "surface.h"

/* ── Basic primitives ────────────────────────────────────────────────── */
void gfx_put_pixel(gfx_surface_t *dst, int32_t x, int32_t y, uint32_t color);
void gfx_draw_line(gfx_surface_t *dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void gfx_draw_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_fill_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

/* ── Circles ──────────────────────────────────────────────────────────── */
void gfx_draw_circle(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void gfx_fill_circle(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r, uint32_t color);

/* ── Rounded rectangles (buttons, windows, cards) ───────────────────────── */
void gfx_draw_round_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t radius, uint32_t color);
void gfx_fill_round_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t radius, uint32_t color);

/* ── Text (8x16 bitmap font, same glyphs as vbe.c) ──────────────────────── */
void gfx_draw_char(gfx_surface_t *dst, int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_text(gfx_surface_t *dst, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg);

/* ── Blitting (surface -> surface copy, e.g. window contents -> screen) ── */
void gfx_blit(gfx_surface_t *dst, int32_t dx, int32_t dy,
              const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t w, int32_t h);

/* Same as gfx_blit but blends with a constant alpha (0 = invisible, 255 = opaque).
 * Useful for drop shadows, fades, translucent panels. */
void gfx_blit_alpha(gfx_surface_t *dst, int32_t dx, int32_t dy,
                     const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t w, int32_t h,
                     uint8_t alpha);

/* ── Gradients ────────────────────────────────────────────────────────── */
void gfx_gradient_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color_top, uint32_t color_bottom, int vertical);

/* ── StretchBlt (scaled blit, nearest-neighbor — no FPU, no acceleration:
 *   exactly how the real Basic Display Adapter's software StretchBlt works) ── */
void gfx_stretch_blit(gfx_surface_t *dst, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
                       const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t sw, int32_t sh);

/* ── Pattern brushes (GDI HS_* hatch style equivalents) ─────────────────── */
typedef enum {
    GFX_HATCH_HORIZONTAL,  /* ---  */
    GFX_HATCH_VERTICAL,    /* |||  */
    GFX_HATCH_FDIAGONAL,   /* ///  */
    GFX_HATCH_BDIAGONAL,   /* \\\  */
    GFX_HATCH_CROSS,       /* +++  */
    GFX_HATCH_DIAGCROSS,   /* xxx  */
} gfx_hatch_t;

void gfx_fill_hatched(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t fg, uint32_t bg, gfx_hatch_t style);

#endif /* GFX2D_H */
