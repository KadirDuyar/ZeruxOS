/* =============================================================================
 * ZeruX OS — PNG Image Loader
 * File: kernel/include/png.h
 * =============================================================================
 * Loads PNG files from the VFS into a gfx_surface_t, using inflate.h for
 * the DEFLATE/zlib decompression step.
 *
 * SUPPORTED (covers the overwhelming majority of real-world PNGs — icons,
 * logos, screenshots exported by any normal tool):
 *   - Color types 0 (grayscale), 2 (RGB), 3 (palette/PLTE), 4 (gray+alpha),
 *     6 (RGBA)
 *   - 8 bits per channel
 *   - Non-interlaced (interlace method 0)
 *   - All 5 PNG filter types (None/Sub/Up/Average/Paeth) per scanline
 *
 * NOT SUPPORTED (clear error returned, nothing silently wrong):
 *   - Bit depths other than 8 (1/2/4/16-bit PNGs)
 *   - Adam7 interlacing
 *   - Per-pixel alpha compositing: gfx_surface_t (surface.h) stores
 *     0x00RRGGBB with no alpha channel, so any alpha in the source PNG is
 *     read but discarded — the image comes out fully opaque. Adding a
 *     real alpha channel to the Surface type is a natural next step if
 *     you need translucent PNGs (drop shadows, fades) later.
 *   - CRC32 chunk verification (skipped for simplicity — a corrupt file
 *     will typically just fail to decompress cleanly instead)
 * =============================================================================
 */

#ifndef PNG_H
#define PNG_H

#include "surface.h"

/* Loads a PNG file from the given VFS path. Returns an owned gfx_surface_t*
 * (free with surface_destroy() when done), or NULL on any failure
 * (file not found / not a PNG / unsupported feature — check the serial
 * log, png_load() explains exactly what it rejected and why). */
gfx_surface_t *png_load(const char *path);

#endif /* PNG_H */
