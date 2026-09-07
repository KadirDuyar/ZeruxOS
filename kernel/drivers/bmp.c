/* =============================================================================
 * ZeruX OS — BMP Image Loader
 * File: kernel/drivers/bmp.c
 * =============================================================================
 */

#include "bmp.h"
#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include <stddef.h>

/* Little-endian field readers — BMP is always little-endian regardless of
 * host byte order, and we can't assume struct packing matches on-disk
 * layout across compilers, so we read byte-by-byte. */
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rd_i32(const uint8_t *p) { return (int32_t)rd_u32(p); }

gfx_surface_t *bmp_load(const char *path) {
    if (!path) return NULL;

    vfs_node_t *node = vfs_lookup(path);
    if (!node || node->length < 54) {
        serial_printf("[BMP] '%s' not found or too small to be a BMP.\n", path);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)kmalloc(node->length);
    if (!buf) {
        serial_printf("[BMP] Out of memory reading '%s' (%u bytes).\n", path, node->length);
        return NULL;
    }

    int32_t fd = vfs_open(path, 0);
    if (fd < 0) {
        serial_printf("[BMP] vfs_open('%s') failed.\n", path);
        kfree(buf);
        return NULL;
    }
    int32_t got = vfs_read(fd, buf, node->length);
    vfs_close(fd);
    if (got < (int32_t)node->length) {
        serial_printf("[BMP] Short read on '%s' (%d/%u bytes).\n", path, got, node->length);
        kfree(buf);
        return NULL;
    }

    /* ── BITMAPFILEHEADER (14 bytes) ── */
    if (buf[0] != 'B' || buf[1] != 'M') {
        serial_printf("[BMP] '%s': not a BMP file (bad magic).\n", path);
        kfree(buf);
        return NULL;
    }
    uint32_t pixel_offset = rd_u32(buf + 10);

    /* ── BITMAPINFOHEADER (>= 40 bytes) ── */
    uint32_t dib_size = rd_u32(buf + 14);
    if (dib_size < 40 || node->length < 14 + dib_size) {
        serial_printf("[BMP] '%s': unsupported/truncated DIB header (size=%u).\n", path, dib_size);
        kfree(buf);
        return NULL;
    }

    int32_t  width       = rd_i32(buf + 18);
    int32_t  height_raw   = rd_i32(buf + 22);
    uint16_t planes        = rd_u16(buf + 26);
    uint16_t bpp            = rd_u16(buf + 28);
    uint32_t compression     = rd_u32(buf + 30);

    bool top_down = (height_raw < 0);
    int32_t height = top_down ? -height_raw : height_raw;

    if (planes != 1 || compression != 0 /* BI_RGB */ || (bpp != 24 && bpp != 32) ||
        width <= 0 || height <= 0) {
        serial_printf("[BMP] '%s': unsupported format (bpp=%u, compression=%u). "
                      "Only uncompressed 24/32bpp BI_RGB is supported.\n", path, bpp, compression);
        kfree(buf);
        return NULL;
    }

    gfx_surface_t *surf = surface_create((uint32_t)width, (uint32_t)height);
    if (!surf) {
        serial_printf("[BMP] '%s': failed to allocate %dx%d surface.\n", path, width, height);
        kfree(buf);
        return NULL;
    }

    uint32_t bytes_per_px = bpp / 8;
    uint32_t row_bytes     = ((uint32_t)width * bytes_per_px + 3) & ~3u; /* rows padded to 4 bytes */

    if (node->length < pixel_offset + (uint64_t)row_bytes * (uint64_t)height) {
        serial_printf("[BMP] '%s': pixel data runs past end of file.\n", path);
        surface_destroy(surf);
        kfree(buf);
        return NULL;
    }

    for (int32_t row = 0; row < height; row++) {
        /* BMP rows are bottom-up by default; only flip if NOT already top-down */
        int32_t src_row = top_down ? row : (height - 1 - row);
        const uint8_t *srow = buf + pixel_offset + (uint32_t)src_row * row_bytes;
        uint32_t *drow = surf->pixels + (uint32_t)row * surf->pitch;

        for (int32_t col = 0; col < width; col++) {
            const uint8_t *px = srow + (uint32_t)col * bytes_per_px;
            uint8_t b = px[0], g = px[1], r = px[2]; /* BMP stores BGR(A) */
            drow[col] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(buf);
    serial_printf("[BMP] Loaded '%s': %dx%d, %ubpp.\n", path, width, height, bpp);
    return surf;
}
