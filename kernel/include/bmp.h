/* =============================================================================
 * ZeruX OS — BMP Image Loader
 * File: kernel/include/bmp.h
 * =============================================================================
 * Loads uncompressed (BI_RGB) 24bpp or 32bpp Windows BMP files from the VFS
 * into a gfx_surface_t. This is intentionally NOT a PNG decoder — PNG needs
 * a DEFLATE/zlib inflate implementation, which is a separate, much larger
 * task. BMP has no compression to worry about, so it's a same-day win for
 * getting real wallpaper/icon/logo images on screen.
 * =============================================================================
 */

#ifndef BMP_H
#define BMP_H

#include "surface.h"

/* Loads a BMP file from the given VFS path. Returns an owned gfx_surface_t*
 * (free with surface_destroy() when done), or NULL on any failure
 * (file not found, unsupported format — compressed / paletted / <24bpp). */
gfx_surface_t *bmp_load(const char *path);

#endif /* BMP_H */
