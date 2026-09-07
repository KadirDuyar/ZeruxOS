/* =============================================================================
 * ZeruX OS — Framebuffer Manager (Graphics Subsystem, Stage 1)
 * File: kernel/drivers/framebuffer.c
 * =============================================================================
 */

#include "framebuffer.h"
#include "dirty_rect.h"
#include "vbe.h"
#include "serial.h"
#include <stddef.h>

static gfx_surface_t g_screen_surface;
static bool          g_ready = false;

void fb_init(void) {
    if (!g_vbe_enabled) {
        serial_printf("[FB] ERROR: vbe_init() must succeed before fb_init() runs.\n");
        g_ready = false;
        return;
    }

    /* The screen surface is just a view over the existing VBE shadow
     * buffer — no extra copy, no extra allocation. */
    surface_wrap(&g_screen_surface, g_vbe_shadow, g_vbe_width, g_vbe_height, g_vbe_width);

    dirty_init(g_vbe_width, g_vbe_height);

    g_ready = true;
    serial_printf("[FB] Framebuffer Manager ready: %ux%ux%u (back-buffer = VBE shadow)\n",
                  g_vbe_width, g_vbe_height, g_vbe_bpp);
}

bool fb_is_ready(void) {
    return g_ready;
}

fb_info_t fb_get_info(void) {
    fb_info_t info;
    info.width  = g_vbe_width;
    info.height = g_vbe_height;
    info.bpp    = g_vbe_bpp;
    info.pitch  = g_vbe_pitch;
    return info;
}

gfx_surface_t *fb_get_screen_surface(void) {
    return g_ready ? &g_screen_surface : NULL;
}

/* Copies one screen-space rectangle from the shadow buffer to real VRAM,
 * honoring the *real* hardware pitch (g_vbe_pitch, in bytes) which may
 * differ from the shadow buffer's tightly-packed pitch. */
static void present_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)g_vbe_width)  w = (int32_t)g_vbe_width  - x;
    if (y + h > (int32_t)g_vbe_height) h = (int32_t)g_vbe_height - y;
    if (w <= 0 || h <= 0) return;

    for (int32_t row = 0; row < h; row++) {
        uint32_t   sy  = (uint32_t)(y + row);
        uint32_t  *src = g_vbe_shadow + (sy * g_vbe_width) + (uint32_t)x;
        uint8_t   *dst = (uint8_t *)g_vbe_buffer + (sy * g_vbe_pitch) + ((uint32_t)x * 4u);
        vbe_movsl(dst, src, (uint32_t)w);
    }
}

void fb_present(void) {
    if (!g_ready) return;

    if (dirty_is_full_screen()) {
        present_rect(0, 0, (int32_t)g_vbe_width, (int32_t)g_vbe_height);
    } else {
        uint32_t n = dirty_count();
        for (uint32_t i = 0; i < n; i++) {
            dirty_rect_t r;
            if (dirty_get(i, &r)) {
                present_rect(r.x, r.y, r.w, r.h);
            }
        }
    }
    dirty_reset();
}

void fb_present_full(void) {
    dirty_mark_all();
    fb_present();
}
