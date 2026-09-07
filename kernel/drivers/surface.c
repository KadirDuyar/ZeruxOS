/* =============================================================================
 * ZeruX OS — Surface Abstraction (Graphics Subsystem, Stage 1)
 * File: kernel/drivers/surface.c
 * =============================================================================
 */

#include "surface.h"
#include "kheap.h"
#include <stddef.h>

gfx_surface_t *surface_create(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return NULL;

    gfx_surface_t *surf = (gfx_surface_t *)kzalloc(sizeof(gfx_surface_t));
    if (!surf) return NULL;

    uint32_t *pixels = (uint32_t *)kzalloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) {
        kfree(surf);
        return NULL;
    }

    surf->pixels      = pixels;
    surf->width        = width;
    surf->height       = height;
    surf->pitch        = width; /* tightly packed, no row padding */
    surf->owns_memory  = true;
    surface_reset_clip(surf);
    return surf;
}

void surface_destroy(gfx_surface_t *surf) {
    if (!surf) return;
    if (surf->owns_memory && surf->pixels) {
        kfree(surf->pixels);
    }
    kfree(surf);
}

void surface_wrap(gfx_surface_t *out, uint32_t *pixels,
                   uint32_t width, uint32_t height, uint32_t pitch) {
    if (!out) return;
    out->pixels      = pixels;
    out->width        = width;
    out->height       = height;
    out->pitch        = pitch;
    out->owns_memory  = false;
    out->is_dirty     = false;
    surface_reset_clip(out);
}

void surface_reset_clip(gfx_surface_t *surf) {
    if (!surf) return;
    surf->clip_x = 0;
    surf->clip_y = 0;
    surf->clip_w = (int32_t)surf->width;
    surf->clip_h = (int32_t)surf->height;
}

void surface_set_clip(gfx_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!surf) return;

    /* Intersect [x, x+w) with the current clip rect */
    int32_t x0 = (x > surf->clip_x) ? x : surf->clip_x;
    int32_t y0 = (y > surf->clip_y) ? y : surf->clip_y;
    int32_t x1 = ((x + w) < (surf->clip_x + surf->clip_w)) ? (x + w) : (surf->clip_x + surf->clip_w);
    int32_t y1 = ((y + h) < (surf->clip_y + surf->clip_h)) ? (y + h) : (surf->clip_y + surf->clip_h);

    /* Clamp to surface bounds too, in case the caller passed something silly */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)surf->width)  x1 = (int32_t)surf->width;
    if (y1 > (int32_t)surf->height) y1 = (int32_t)surf->height;

    surf->clip_x = x0;
    surf->clip_y = y0;
    surf->clip_w = (x1 > x0) ? (x1 - x0) : 0;
    surf->clip_h = (y1 > y0) ? (y1 - y0) : 0;
}

void surface_clear(gfx_surface_t *surf, uint32_t color) {
    if (!surf || !surf->pixels) return;
    for (uint32_t y = 0; y < surf->height; y++) {
        uint32_t *row = surf->pixels + (y * surf->pitch);
        for (uint32_t x = 0; x < surf->width; x++) {
            row[x] = color;
        }
    }
}
