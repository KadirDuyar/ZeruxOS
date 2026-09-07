/* =============================================================================
 * ZeruX OS — 2D Graphics Library (Graphics Subsystem, Stage 1)
 * File: kernel/drivers/gfx2d.c
 * =============================================================================
 */

#include "gfx2d.h"
#include "framebuffer.h"
#include "dirty_rect.h"
#include <stddef.h>

/* font8x16.h lives in kernel/drivers/ alongside this file (quoted-include
 * finds it in the same directory, same as vbe.c does today). */
#include "font8x16.h"

/* =============================================================================
 * Internal helpers
 * =============================================================================
 */
static inline void track_dirty_pixel(gfx_surface_t *dst, int32_t x, int32_t y);

static inline void set_px(gfx_surface_t *dst, int32_t x, int32_t y, uint32_t color) {
    if (!dst || !dst->pixels) return;
    /* clip_x/clip_y/clip_w/clip_h is always a subset of [0,width)x[0,height),
     * so testing against it alone is sufficient (see surface_set_clip). */
    if (x < dst->clip_x || y < dst->clip_y) return;
    if (x >= dst->clip_x + dst->clip_w || y >= dst->clip_y + dst->clip_h) return;
    uint32_t *p = &dst->pixels[(uint32_t)y * dst->pitch + (uint32_t)x];
    if (*p != color) {
        *p = color;
        track_dirty_pixel(dst, x, y);
    }
}

/* Only the live screen surface is tracked for partial VRAM presentation;
 * off-screen surfaces (future window buffers) don't need this yet. */

static inline void track_dirty_pixel(gfx_surface_t *dst, int32_t x, int32_t y) {
    if (!dst->is_dirty) {
        dst->dirty_x0 = x; dst->dirty_y0 = y;
        dst->dirty_x1 = x + 1; dst->dirty_y1 = y + 1;
        dst->is_dirty = true;
    } else {
        if (x < dst->dirty_x0) dst->dirty_x0 = x;
        if (y < dst->dirty_y0) dst->dirty_y0 = y;
        if (x + 1 > dst->dirty_x1) dst->dirty_x1 = x + 1;
        if (y + 1 > dst->dirty_y1) dst->dirty_y1 = y + 1;
    }
}

static void mark_if_screen(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h) {
    gfx_surface_t *screen = fb_get_screen_surface();
    if (screen && dst == screen) {
        dirty_mark(x, y, w, h);
    }
    // Also if it's the screen, clear the internal dirty flag since it was flushed
    if (screen && dst == screen) {
        dst->is_dirty = false;
    }
}

/* =============================================================================
 * Basic primitives
 * =============================================================================
 */
void gfx_put_pixel(gfx_surface_t *dst, int32_t x, int32_t y, uint32_t color) {

    mark_if_screen(dst, x, y, 1, 1);
}

void gfx_draw_line(gfx_surface_t *dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!dst || !dst->pixels) return;

    int32_t dx  =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy  = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int32_t sx  = (x0 < x1) ? 1 : -1;
    int32_t sy  = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    int32_t cx = x0, cy = y0;
    while (1) {
        set_px(dst, cx, cy, color);
        if (cx == x1 && cy == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }

    int32_t minx = (x0 < x1) ? x0 : x1;
    int32_t miny = (y0 < y1) ? y0 : y1;
    int32_t maxx = (x0 > x1) ? x0 : x1;
    int32_t maxy = (y0 > y1) ? y0 : y1;
    mark_if_screen(dst, minx, miny, (maxx - minx) + 1, (maxy - miny) + 1);
}

void gfx_fill_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!dst || !dst->pixels) return;

    int32_t cx0 = dst->clip_x, cy0 = dst->clip_y;
    int32_t cx1 = dst->clip_x + dst->clip_w, cy1 = dst->clip_y + dst->clip_h;
    if (x < cx0) { w -= (cx0 - x); x = cx0; }
    if (y < cy0) { h -= (cy0 - y); y = cy0; }
    if (x + w > cx1) w = cx1 - x;
    if (y + h > cy1) h = cy1 - y;
    if (w <= 0 || h <= 0) return;

    for (int32_t row = 0; row < h; row++) {
        uint32_t *dstrow = dst->pixels + (uint32_t)(y + row) * dst->pitch + (uint32_t)x;
        for (int32_t col = 0; col < w; col++) {
            if (dstrow[col] != color) {
                dstrow[col] = color;
                track_dirty_pixel(dst, x + col, y + row);
            }
        }
    }
    mark_if_screen(dst, x, y, w, h);
}

void gfx_draw_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;
    /* Top / bottom edges */
    for (int32_t col = 0; col < w; col++) {
        set_px(dst, x + col, y, color);
        set_px(dst, x + col, y + h - 1, color);
    }
    /* Left / right edges */
    for (int32_t row = 0; row < h; row++) {
        set_px(dst, x, y + row, color);
        set_px(dst, x + w - 1, y + row, color);
    }
    mark_if_screen(dst, x, y, w, h);
}

/* =============================================================================
 * Circles (integer-only midpoint / scanline algorithms — no FPU needed)
 * =============================================================================
 */
void gfx_draw_circle(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    if (!dst || !dst->pixels || r <= 0) return;

    int32_t x = r, y = 0, err = 0;
    while (x >= y) {
        set_px(dst, cx + x, cy + y, color); set_px(dst, cx + y, cy + x, color);
        set_px(dst, cx - y, cy + x, color); set_px(dst, cx - x, cy + y, color);
        set_px(dst, cx - x, cy - y, color); set_px(dst, cx - y, cy - x, color);
        set_px(dst, cx + y, cy - x, color); set_px(dst, cx + x, cy - y, color);
        y++;
        if (err <= 0) { err += 2 * y + 1; }
        if (err > 0)  { x--; err -= 2 * x + 1; }
    }
    mark_if_screen(dst, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

void gfx_fill_circle(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    if (!dst || !dst->pixels || r <= 0) return;

    int32_t x = r;
    for (int32_t y = 0; y <= r; y++) {
        while (x > 0 && (x * x + y * y) > r * r) x--;
        for (int32_t px = -x; px <= x; px++) {
            set_px(dst, cx + px, cy + y, color);
            set_px(dst, cx + px, cy - y, color);
        }
    }
    mark_if_screen(dst, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

/* One quadrant of a filled circle, offset from (cx,cy) in direction (sx,sy) (+-1).
 * Used to build rounded-rectangle corners without bulging outside the rect. */
static void fill_quadrant(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r,
                           uint32_t color, int32_t sx, int32_t sy) {
    int32_t x = r;
    for (int32_t y = 0; y <= r; y++) {
        while (x > 0 && (x * x + y * y) > r * r) x--;
        for (int32_t px = 0; px <= x; px++) {
            set_px(dst, cx + sx * px, cy + sy * y, color);
        }
    }
}

/* Single boundary pixel per scanline of one quadrant's arc — used for outlines. */
static void draw_quadrant_arc(gfx_surface_t *dst, int32_t cx, int32_t cy, int32_t r,
                               uint32_t color, int32_t sx, int32_t sy) {
    int32_t x = r;
    for (int32_t y = 0; y <= r; y++) {
        while (x > 0 && (x * x + y * y) > r * r) x--;
        set_px(dst, cx + sx * x, cy + sy * y, color);
    }
}

/* =============================================================================
 * Rounded rectangles
 * =============================================================================
 */
void gfx_fill_round_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t radius, uint32_t color) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;

    int32_t maxr = (w < h ? w : h) / 2;
    if (radius > maxr) radius = maxr;
    if (radius < 0) radius = 0;

    if (radius == 0) { gfx_fill_rect(dst, x, y, w, h, color); return; }

    /* Center cross: covers everything except the four corner squares */
    gfx_fill_rect(dst, x + radius, y, w - 2 * radius, h, color);
    gfx_fill_rect(dst, x, y + radius, radius, h - 2 * radius, color);
    gfx_fill_rect(dst, x + w - radius, y + radius, radius, h - 2 * radius, color);

    /* Four rounded corners */
    fill_quadrant(dst, x + radius,         y + radius,         radius, color, -1, -1);
    fill_quadrant(dst, x + w - radius - 1, y + radius,         radius, color, +1, -1);
    fill_quadrant(dst, x + radius,         y + h - radius - 1, radius, color, -1, +1);
    fill_quadrant(dst, x + w - radius - 1, y + h - radius - 1, radius, color, +1, +1);

    mark_if_screen(dst, x, y, w, h);
}

void gfx_draw_round_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t radius, uint32_t color) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;

    int32_t maxr = (w < h ? w : h) / 2;
    if (radius > maxr) radius = maxr;
    if (radius < 0) radius = 0;

    if (radius == 0) { gfx_draw_rect(dst, x, y, w, h, color); return; }

    /* Straight edges between the corners */
    gfx_fill_rect(dst, x + radius, y,         w - 2 * radius, 1, color);
    gfx_fill_rect(dst, x + radius, y + h - 1, w - 2 * radius, 1, color);
    gfx_fill_rect(dst, x,          y + radius, 1, h - 2 * radius, color);
    gfx_fill_rect(dst, x + w - 1,  y + radius, 1, h - 2 * radius, color);

    /* Four corner arcs */
    draw_quadrant_arc(dst, x + radius,         y + radius,         radius, color, -1, -1);
    draw_quadrant_arc(dst, x + w - radius - 1, y + radius,         radius, color, +1, -1);
    draw_quadrant_arc(dst, x + radius,         y + h - radius - 1, radius, color, -1, +1);
    draw_quadrant_arc(dst, x + w - radius - 1, y + h - radius - 1, radius, color, +1, +1);

    mark_if_screen(dst, x, y, w, h);
}

/* =============================================================================
 * Text (reuses the same 8x16 bitmap font as vbe.c)
 * =============================================================================
 */
void gfx_draw_char(gfx_surface_t *dst, int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!dst || !dst->pixels) return;
    uint8_t idx = (uint8_t)c;
    const uint8_t *glyph = font8x16[idx];
    for (int32_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            set_px(dst, x + col, y + row, color);
        }
    }
    mark_if_screen(dst, x, y, 8, 16);
}

void gfx_draw_text(gfx_surface_t *dst, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg) {
    if (!dst || !dst->pixels || !str) return;
    int32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += 16;
        } else {
            gfx_draw_char(dst, cx, y, *str, fg, bg);
            cx += 8;
        }
        str++;
    }
}

/* =============================================================================
 * Blitting
 * =============================================================================
 */
void gfx_blit(gfx_surface_t *dst, int32_t dx, int32_t dy,
              const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t w, int32_t h) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    if ((uint32_t)src->pixels < 0x100000 || (uint32_t)dst->pixels < 0x100000) return;

    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > (int32_t)src->width)  w = (int32_t)src->width  - sx;
    if (sy + h > (int32_t)src->height) h = (int32_t)src->height - sy;

    {
        int32_t cx0 = dst->clip_x, cy0 = dst->clip_y;
        int32_t cx1 = dst->clip_x + dst->clip_w, cy1 = dst->clip_y + dst->clip_h;
        if (dx < cx0) { w -= (cx0 - dx); sx += (cx0 - dx); dx = cx0; }
        if (dy < cy0) { h -= (cy0 - dy); sy += (cy0 - dy); dy = cy0; }
        if (dx + w > cx1) w = cx1 - dx;
        if (dy + h > cy1) h = cy1 - dy;
    }

    if (w <= 0 || h <= 0) return;

    for (int32_t row = 0; row < h; row++) {
        const uint32_t *srow = src->pixels + (uint32_t)(sy + row) * src->pitch + (uint32_t)sx;
        uint32_t       *drow = dst->pixels + (uint32_t)(dy + row) * dst->pitch + (uint32_t)dx;
        for (int32_t col = 0; col < w; col++) {
            if (drow[col] != srow[col]) {
                drow[col] = srow[col];
                track_dirty_pixel(dst, dx + col, dy + row);
            }
        }
    }
    mark_if_screen(dst, dx, dy, w, h);
}

void gfx_blit_alpha(gfx_surface_t *dst, int32_t dx, int32_t dy,
                     const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t w, int32_t h,
                     uint8_t alpha) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    if ((uint32_t)src->pixels < 0x100000 || (uint32_t)dst->pixels < 0x100000) return;
    if (alpha == 255) { gfx_blit(dst, dx, dy, src, sx, sy, w, h); return; }
    if (alpha == 0) return;

    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > (int32_t)src->width)  w = (int32_t)src->width  - sx;
    if (sy + h > (int32_t)src->height) h = (int32_t)src->height - sy;

    {
        int32_t cx0 = dst->clip_x, cy0 = dst->clip_y;
        int32_t cx1 = dst->clip_x + dst->clip_w, cy1 = dst->clip_y + dst->clip_h;
        if (dx < cx0) { w -= (cx0 - dx); sx += (cx0 - dx); dx = cx0; }
        if (dy < cy0) { h -= (cy0 - dy); sy += (cy0 - dy); dy = cy0; }
        if (dx + w > cx1) w = cx1 - dx;
        if (dy + h > cy1) h = cy1 - dy;
    }

    if (w <= 0 || h <= 0) return;

    uint32_t inv_alpha = 255u - alpha;

    for (int32_t row = 0; row < h; row++) {
        const uint32_t *srow = src->pixels + (uint32_t)(sy + row) * src->pitch + (uint32_t)sx;
        uint32_t       *drow = dst->pixels + (uint32_t)(dy + row) * dst->pitch + (uint32_t)dx;
        for (int32_t col = 0; col < w; col++) {
            uint32_t s = srow[col];
            uint32_t d = drow[col];

            uint32_t sr = (s >> 16) & 0xFF, sg = (s >> 8) & 0xFF, sb = s & 0xFF;
            uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;

            uint32_t rr = (sr * alpha + dr * inv_alpha) / 255u;
            uint32_t rg = (sg * alpha + dg * inv_alpha) / 255u;
            uint32_t rb = (sb * alpha + db * inv_alpha) / 255u;

            uint32_t new_color = (rr << 16) | (rg << 8) | rb;
            if (drow[col] != new_color) {
                drow[col] = new_color;
                track_dirty_pixel(dst, dx + col, dy + row);
            }
        }
    }
    mark_if_screen(dst, dx, dy, w, h);
}

/* =============================================================================
 * Gradients
 * =============================================================================
 */
void gfx_gradient_rect(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color_top, uint32_t color_bottom, int vertical) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;

    int32_t r0 = (color_top >> 16) & 0xFF, g0 = (color_top >> 8) & 0xFF, b0 = color_top & 0xFF;
    int32_t r1 = (color_bottom >> 16) & 0xFF, g1 = (color_bottom >> 8) & 0xFF, b1 = color_bottom & 0xFF;

    if (vertical) {
        int32_t steps = (h > 1) ? (h - 1) : 1;
        for (int32_t row = 0; row < h; row++) {
            int32_t r = r0 + ((r1 - r0) * row) / steps;
            int32_t g = g0 + ((g1 - g0) * row) / steps;
            int32_t b = b0 + ((b1 - b0) * row) / steps;
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            gfx_fill_rect(dst, x, y + row, w, 1, color);
        }
    } else {
        int32_t steps = (w > 1) ? (w - 1) : 1;
        for (int32_t col = 0; col < w; col++) {
            int32_t r = r0 + ((r1 - r0) * col) / steps;
            int32_t g = g0 + ((g1 - g0) * col) / steps;
            int32_t b = b0 + ((b1 - b0) * col) / steps;
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            gfx_fill_rect(dst, x + col, y, 1, h, color);
        }
    }
    mark_if_screen(dst, x, y, w, h);
}

/* =============================================================================
 * StretchBlt — scaled blit (nearest-neighbor, fixed-point, no FPU)
 * =============================================================================
 */
void gfx_stretch_blit(gfx_surface_t *dst, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
                       const gfx_surface_t *src, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    if ((uint32_t)src->pixels < 0x100000 || (uint32_t)dst->pixels < 0x100000) return;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    /* 16.16 fixed-point scale factors — avoids needing the FPU in the kernel */
    int32_t x_ratio = (sw << 16) / dw;
    int32_t y_ratio = (sh << 16) / dh;

    for (int32_t row = 0; row < dh; row++) {
        int32_t srcy = sy + ((row * y_ratio) >> 16);
        if (srcy < 0 || srcy >= (int32_t)src->height) continue;
        const uint32_t *srow = src->pixels + (uint32_t)srcy * src->pitch;

        for (int32_t col = 0; col < dw; col++) {
            int32_t srcx = sx + ((col * x_ratio) >> 16);
            if (srcx < 0 || srcx >= (int32_t)src->width) continue;
            uint32_t src_color = srow[srcx];
            uint32_t alpha = src_color >> 24;
            if (alpha == 0) continue;
            
            if (alpha == 255) {
                set_px(dst, dx + col, dy + row, src_color & 0xFFFFFF);
            } else {
                int32_t screen_x = dx + col;
                int32_t screen_y = dy + row;
                if (screen_x < dst->clip_x || screen_y < dst->clip_y) continue;
                if (screen_x >= dst->clip_x + dst->clip_w || screen_y >= dst->clip_y + dst->clip_h) continue;
                
                uint32_t *dp = &dst->pixels[(uint32_t)screen_y * dst->pitch + (uint32_t)screen_x];
                uint32_t dest_color = *dp;
                
                uint32_t inv_alpha = 255 - alpha;
                uint32_t sr = (src_color >> 16) & 0xFF;
                uint32_t sg = (src_color >> 8) & 0xFF;
                uint32_t sb = src_color & 0xFF;
                uint32_t dr = (dest_color >> 16) & 0xFF;
                uint32_t dg = (dest_color >> 8) & 0xFF;
                uint32_t db = dest_color & 0xFF;

                uint32_t rr = (sr * alpha + dr * inv_alpha) / 255u;
                uint32_t rg = (sg * alpha + dg * inv_alpha) / 255u;
                uint32_t rb = (sb * alpha + db * inv_alpha) / 255u;

                uint32_t new_color = (rr << 16) | (rg << 8) | rb;
                if (*dp != new_color) {
                    *dp = new_color;
                    track_dirty_pixel(dst, screen_x, screen_y);
                }
            }
        }
    }
    mark_if_screen(dst, dx, dy, dw, dh);
}

/* =============================================================================
 * Hatch pattern brushes (classic GDI HS_* styles, 8x8 tile anchored to the
 * top-left corner of the fill rect — same convention GDI brushes use)
 * =============================================================================
 */
static bool hatch_test(gfx_hatch_t style, int32_t lx, int32_t ly) {
    switch (style) {
        case GFX_HATCH_HORIZONTAL: return ly == 3;
        case GFX_HATCH_VERTICAL:   return lx == 3;
        case GFX_HATCH_FDIAGONAL:  return ((lx + ly) & 7) == 0;
        case GFX_HATCH_BDIAGONAL:  return ((lx - ly) & 7) == 0;
        case GFX_HATCH_CROSS:      return (lx == 3) || (ly == 3);
        case GFX_HATCH_DIAGCROSS:  return (((lx + ly) & 7) == 0) || (((lx - ly) & 7) == 0);
        default:                   return false;
    }
}

void gfx_fill_hatched(gfx_surface_t *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                       uint32_t fg, uint32_t bg, gfx_hatch_t style) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;

    for (int32_t row = 0; row < h; row++) {
        int32_t ly = row & 7;
        for (int32_t col = 0; col < w; col++) {
            int32_t lx = col & 7;
            set_px(dst, x + col, y + row, hatch_test(style, lx, ly) ? fg : bg);
        }
    }
    mark_if_screen(dst, x, y, w, h);
}
