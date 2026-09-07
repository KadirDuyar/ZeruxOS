/* =============================================================================
 * ZeruX OS — PNG Image Loader
 * File: kernel/drivers/png.c
 * =============================================================================
 */

#include "png.h"
#include "inflate.h"
#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include <stddef.h>

/* Sanity cap on decoded image dimensions — guards against a malformed or
 * hostile IHDR claiming an enormous image and blowing the kernel heap
 * before we ever get to the (self-limiting) inflate step. Raise this if
 * you genuinely need bigger images and have the heap for it. */
#define PNG_MAX_DIM 4096

static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

static uint32_t rd_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int chunk_is(const uint8_t *type, const char *name) {
    return type[0] == name[0] && type[1] == name[1] && type[2] == name[2] && type[3] == name[3];
}

static uint8_t paeth_predict(uint8_t a, uint8_t b, uint8_t c) {
    int32_t p  = (int32_t)a + (int32_t)b - (int32_t)c;
    int32_t pa = p - (int32_t)a; if (pa < 0) pa = -pa;
    int32_t pb = p - (int32_t)b; if (pb < 0) pb = -pb;
    int32_t pc = p - (int32_t)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

gfx_surface_t *png_load(const char *path) {
    if (!path) return NULL;

    vfs_node_t *node = vfs_lookup(path);
    if (!node || node->length < 8) {
        serial_printf("[PNG] '%s' not found or too small.\n", path);
        return NULL;
    }

    uint8_t *file = (uint8_t *)kmalloc(node->length);
    if (!file) { serial_printf("[PNG] Out of memory reading '%s'.\n", path); return NULL; }

    int32_t fd = vfs_open(path, 0);
    if (fd < 0) { serial_printf("[PNG] vfs_open('%s') failed.\n", path); kfree(file); return NULL; }
    int32_t got_file = vfs_read(fd, file, node->length);
    vfs_close(fd);
    if (got_file < (int32_t)node->length) {
        serial_printf("[PNG] Short read on '%s'.\n", path);
        kfree(file);
        return NULL;
    }

    for (int i = 0; i < 8; i++) {
        if (file[i] != PNG_SIGNATURE[i]) {
            serial_printf("[PNG] '%s': not a PNG file (bad signature).\n", path);
            kfree(file);
            return NULL;
        }
    }

    /* ── Pass 1: find IHDR, PLTE, and the total size of all IDAT chunks ── */
    uint32_t width = 0, height = 0;
    uint8_t  bit_depth = 0, color_type = 0;
    int      have_ihdr = 0;
    uint8_t  palette[256 * 3];
    uint32_t palette_count = 0;
    uint32_t idat_total = 0;

    uint32_t pos = 8;
    while (pos + 8 <= node->length) {
        uint32_t clen = rd_u32be(file + pos);
        const uint8_t *ctype = file + pos + 4;
        uint32_t data_off = pos + 8;
        if ((uint64_t)data_off + clen + 4 > node->length) break; /* truncated file */

        if (chunk_is(ctype, "IHDR")) {
            if (clen < 13) { serial_printf("[PNG] '%s': malformed IHDR.\n", path); kfree(file); return NULL; }
            width      = rd_u32be(file + data_off);
            height     = rd_u32be(file + data_off + 4);
            bit_depth  = file[data_off + 8];
            color_type = file[data_off + 9];
            uint8_t compression = file[data_off + 10];
            uint8_t filter_method = file[data_off + 11];
            uint8_t interlace = file[data_off + 12];
            if (compression != 0 || filter_method != 0) {
                serial_printf("[PNG] '%s': unrecognized compression/filter method.\n", path);
                kfree(file); return NULL;
            }
            if (interlace != 0) {
                serial_printf("[PNG] '%s': Adam7-interlaced PNGs are not supported yet. "
                              "Re-export as non-interlaced.\n", path);
                kfree(file); return NULL;
            }
            have_ihdr = 1;
        } else if (chunk_is(ctype, "PLTE")) {
            palette_count = clen / 3;
            if (palette_count > 256) palette_count = 256;
            for (uint32_t i = 0; i < palette_count * 3 && i < clen; i++) palette[i] = file[data_off + i];
        } else if (chunk_is(ctype, "IDAT")) {
            idat_total += clen;
        } else if (chunk_is(ctype, "IEND")) {
            break;
        }
        pos = data_off + clen + 4;
    }

    if (!have_ihdr || width == 0 || height == 0) {
        serial_printf("[PNG] '%s': missing/invalid IHDR.\n", path);
        kfree(file); return NULL;
    }
    if (width > PNG_MAX_DIM || height > PNG_MAX_DIM) {
        serial_printf("[PNG] '%s': %ux%u exceeds the %ux%u safety cap.\n", path, width, height, PNG_MAX_DIM, PNG_MAX_DIM);
        kfree(file); return NULL;
    }
    if (bit_depth != 8) {
        serial_printf("[PNG] '%s': %u-bit depth not supported (only 8-bit is). "
                      "Re-export as 8-bit.\n", path, bit_depth);
        kfree(file); return NULL;
    }

    int channels;
    switch (color_type) {
        case 0: channels = 1; break; /* grayscale       */
        case 2: channels = 3; break; /* RGB             */
        case 3: channels = 1; break; /* palette index   */
        case 4: channels = 2; break; /* grayscale+alpha */
        case 6: channels = 4; break; /* RGBA            */
        default:
            serial_printf("[PNG] '%s': unsupported color type %u.\n", path, color_type);
            kfree(file); return NULL;
    }
    if (color_type == 3 && palette_count == 0) {
        serial_printf("[PNG] '%s': palette color type but no PLTE chunk found.\n", path);
        kfree(file); return NULL;
    }
    if (idat_total < 2) {
        serial_printf("[PNG] '%s': no (or too little) IDAT data.\n", path);
        kfree(file); return NULL;
    }

    /* ── Pass 2: concatenate all IDAT chunks into one contiguous buffer ── */
    uint8_t *idat = (uint8_t *)kmalloc(idat_total);
    if (!idat) { serial_printf("[PNG] Out of memory for IDAT (%u bytes).\n", idat_total); kfree(file); return NULL; }

    uint32_t idat_pos = 0;
    pos = 8;
    while (pos + 8 <= node->length) {
        uint32_t clen = rd_u32be(file + pos);
        const uint8_t *ctype = file + pos + 4;
        uint32_t data_off = pos + 8;
        if ((uint64_t)data_off + clen + 4 > node->length) break;

        if (chunk_is(ctype, "IDAT")) {
            for (uint32_t i = 0; i < clen; i++) idat[idat_pos++] = file[data_off + i];
        } else if (chunk_is(ctype, "IEND")) {
            break;
        }
        pos = data_off + clen + 4;
    }
    kfree(file);

    /* IDAT is a zlib stream (RFC 1950): 2-byte header, DEFLATE data, 4-byte
     * Adler32 trailer. We skip the header and let inflate_raw() stop at the
     * DEFLATE final-block marker on its own — no need to trim the trailer. */
    uint32_t scanline_bytes = width * (uint32_t)channels;
    uint32_t raw_size = height * (1u + scanline_bytes);

    uint8_t *raw = (uint8_t *)kmalloc(raw_size);
    if (!raw) { serial_printf("[PNG] Out of memory for pixel buffer (%u bytes).\n", raw_size); kfree(idat); return NULL; }

    int32_t got = inflate_raw(idat + 2, idat_pos - 2, raw, raw_size);
    kfree(idat);
    if (got != (int32_t)raw_size) {
        serial_printf("[PNG] '%s': inflate failed or size mismatch (got %d, expected %u).\n", path, got, raw_size);
        kfree(raw);
        return NULL;
    }

    /* ── Un-filter each scanline in place (PNG filter types 0-4) ──────── */
    uint32_t bpp = (uint32_t)channels; /* bytes per whole pixel, bit_depth == 8 */
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row_start = raw + y * (1u + scanline_bytes);
        uint8_t filter_type = row_start[0];
        uint8_t *cur  = row_start + 1;
        uint8_t *prev = (y == 0) ? NULL : (row_start - (1u + scanline_bytes) + 1);

        for (uint32_t x = 0; x < scanline_bytes; x++) {
            uint8_t a = (x >= bpp) ? cur[x - bpp] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            uint8_t recon;
            switch (filter_type) {
                case 0: recon = cur[x]; break;
                case 1: recon = (uint8_t)(cur[x] + a); break;
                case 2: recon = (uint8_t)(cur[x] + b); break;
                case 3: recon = (uint8_t)(cur[x] + (uint8_t)(((uint32_t)a + (uint32_t)b) / 2)); break;
                case 4: recon = (uint8_t)(cur[x] + paeth_predict(a, b, c)); break;
                default:
                    serial_printf("[PNG] '%s': invalid filter type %u at row %u.\n", path, filter_type, y);
                    kfree(raw);
                    return NULL;
            }
            cur[x] = recon;
        }
    }

    /* ── Convert unfiltered channel bytes -> 0x00RRGGBB surface ───────── */
    gfx_surface_t *surf = surface_create(width, height);
    if (!surf) { serial_printf("[PNG] '%s': failed to allocate %ux%u surface.\n", path, width, height); kfree(raw); return NULL; }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *cur = raw + y * (1u + scanline_bytes) + 1;
        uint32_t *drow = surf->pixels + y * surf->pitch;

        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *px = cur + x * (uint32_t)channels;
            uint8_t r, g, b, a = 255;
            switch (color_type) {
                case 0: r = g = b = px[0]; break;
                case 2: r = px[0]; g = px[1]; b = px[2]; break;
                case 3: {
                    uint8_t idx = px[0];
                    if (idx < palette_count) { r = palette[idx * 3]; g = palette[idx * 3 + 1]; b = palette[idx * 3 + 2]; }
                    else { r = g = b = 0; }
                    break;
                }
                case 4: {
                    a = px[1];
                    r = g = b = px[0];
                    break;
                }
                default: {
                    if (color_type == 6) {
                        a = px[3];
                        r = px[0]; g = px[1]; b = px[2];
                    } else {
                        r = px[0]; g = px[1]; b = px[2];
                    }
                    break;
                }
            }
            drow[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(raw);
    serial_printf("[PNG] Loaded '%s': %ux%u, color_type=%u, %d bytes decompressed.\n",
                  path, width, height, color_type, got);
    return surf;
}
