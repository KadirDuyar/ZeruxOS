/* =============================================================================
 * ZeruX OS — Compositor (Paint Layer)
 * File: kernel/include/compositor.h
 * =============================================================================
 * Walks the Window Manager's z-order (window.h) back-to-front and paints
 * each window's chrome (title bar, border, close button) plus its content
 * surface onto the screen surface (framebuffer.h), using gfx2d.h.
 *
 * This is a "painter's algorithm" compositor: every compose() call redraws
 * the whole desktop into the back buffer from scratch. That is intentionally
 * simple and correct rather than damage-optimal — at 1024x768 with a
 * handful of windows this costs a few milliseconds of CPU, which is fine
 * for a hobby OS. Partial/damage-based recompositing (only repainting the
 * screen-space union of what actually changed) is a natural follow-up once
 * you have enough windows for the full redraw to visibly cost you frames.
 * =============================================================================
 */

#ifndef COMPOSITOR_H
#define COMPOSITOR_H

/* Repaints the entire desktop (background + all visible windows, in
 * z-order) into the framebuffer's back buffer (fb_get_screen_surface()).
 *
 * IMPORTANT: this does NOT push pixels to real VRAM and does NOT draw the
 * mouse cursor. After calling this, call vbe_refresh_screen() exactly as
 * you do today — that is still what composites the cursor overlay and
 * copies the back buffer to VRAM. This keeps the cursor's existing
 * save/restore logic in vbe.c as the single owner of cursor compositing,
 * per the Stage 1 framebuffer.c design note. */
void compositor_compose(void);

#endif /* COMPOSITOR_H */
