/* =============================================================================
 * ZeruX OS — Framebuffer Manager (Graphics Subsystem, Stage 1)
 * File: kernel/include/framebuffer.h
 * =============================================================================
 * Sits ON TOP of vbe.c instead of replacing it. vbe.c stays exactly what it
 * is today: the low-level display driver that knows about VBE/BGA I/O ports
 * and the physical LFB address. framebuffer.c wraps its shadow buffer as a
 * gfx_surface_t and owns the dirty-rectangle-aware present path.
 *
 * Why separate this from vbe.c at all? Per the roadmap: this is the seam
 * where you'll later plug in Bochs Graphics Adapter / VirtIO GPU / VMware
 * SVGA-II drivers without touching a single line of GUI or gfx2d code —
 * only fb_init()'s internals would change.
 * =============================================================================
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "surface.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch; /* real hardware stride, in BYTES */
} fb_info_t;

/* Call once, AFTER vbe_init() has already succeeded (g_vbe_enabled == 1). */
void fb_init(void);

/* True once fb_init() has run against an active VBE mode. */
bool fb_is_ready(void);

fb_info_t fb_get_info(void);

/* Returns the back-buffer as a surface. gfx2d.c draws into this. Do not
 * free it — it is owned by framebuffer.c and wraps the VBE shadow buffer. */
gfx_surface_t *fb_get_screen_surface(void);

/* Copies every region dirty_mark()'d since the last present to real VRAM.
 * Does NOT draw the mouse cursor — call vbe_refresh_screen() instead (or
 * as well) whenever the visible desktop, cursor included, needs to be
 * shown. See the driver's top-of-file comment for why these are kept
 * separate for now. */
void fb_present(void);

/* Convenience: marks the whole screen dirty, then presents it. */
void fb_present_full(void);

#endif /* FRAMEBUFFER_H */
