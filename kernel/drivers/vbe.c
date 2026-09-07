/* =============================================================================
 * ZeruX OS — VBE (VESA BIOS Extensions) Linear Framebuffer Driver
 * File: kernel/drivers/vbe.c
 *
 * Uses the Bochs/QEMU VBE Dispi Interface (BGA) at I/O ports 0x01CE / 0x01D0.
 * Physical framebuffer address is obtained from the PCI VGA device's BAR0.
 * Then EXPLICITLY mapped into virtual memory page-by-page -- identity mapping
 * only covers ~32 MB and BAR0 is well above that.
 *
 * vbe_refresh_screen() is now dirty-rect aware (kernel/include/dirty_rect.h,
 * part of the Graphics Stage 1 work): instead of always copying the full
 * shadow buffer to VRAM, it flushes only the rects the compositor/gfx2d
 * layer actually marked dirty this frame, PLUS the mouse cursor's old and
 * new footprint (the cursor moves almost every frame and isn't tracked by
 * anything above this file, so it has to mark its own damage here). If
 * dirty_rect.h reports "full screen" (its default until something more
 * specific has been marked -- see dirty_rect.c) or nothing was marked at
 * all besides a stationary cursor, behavior falls back to exactly what it
 * was before: a full-buffer copy, or nothing.
 * =============================================================================
 */

#include "vbe.h"
#include "pci.h"
#include "vmm.h"
#include "serial.h"
#include "boot_info.h"
#include "dirty_rect.h"
#include <stdbool.h>

/* ── Public globals ────────────────────────────────────────────────────────── */
uint16_t  g_vbe_width   = 0;
uint16_t  g_vbe_height  = 0;
uint16_t  g_vbe_bpp     = 0;
uint32_t  g_vbe_pitch   = 0;
uint32_t *g_vbe_buffer  = 0;
int       g_vbe_enabled = 0;

/* ── I/O port helpers ──────────────────────────────────────────────────────── */
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ── vbe_init() ────────────────────────────────────────────────────────────── */
void vbe_init(uint16_t xres, uint16_t yres, uint16_t bpp) {
    (void)xres; (void)yres; (void)bpp; // Parameters ignored, using Real VESA info

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Real Hardware VBE Framebuffer init\n");
    serial_printf("===========================================\n");

    const boot_info_t *info = boot_info_get();
    
    if (info->vbe_mode == 0) {
        serial_printf("[VBE] ERROR: Bootloader failed to set VESA mode!\n");
        g_vbe_enabled = 0;
        return;
    }

    g_vbe_width  = info->vbe_width;
    g_vbe_height = info->vbe_height;
    g_vbe_pitch  = info->vbe_pitch;
    g_vbe_bpp    = info->vbe_bpp;
    
    uint32_t phys_base = info->vbe_phys_base;
    uint32_t size_bytes = g_vbe_pitch * g_vbe_height;

    serial_printf("[VBE] VESA Mode Active: %dx%dx%d (Pitch: %d)\n", 
                  g_vbe_width, g_vbe_height, g_vbe_bpp, g_vbe_pitch);
    serial_printf("[VBE] LFB Physical Base: 0x%x (Size: %d bytes)\n", 
                  phys_base, size_bytes);

    /* 3. Map framebuffer into virtual memory (Page by Page) */
    g_vbe_buffer = (uint32_t *)phys_base; /* Physical == Virtual for now due to Identity Mapping */
    
    /* Ensure pages are mapped */
    uint32_t num_pages = (size_bytes + 4095) / 4096;
    page_directory_t *kpdir = vmm_get_kernel_page_directory();

    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t page_addr = phys_base + (i * 4096);
        vmm_map_page(kpdir, page_addr, page_addr, 
                     VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    }

    serial_printf("[VBE] Framebuffer identity mapped successfully.\n");
    g_vbe_enabled = 1;
}


uint32_t g_vbe_shadow[1024 * 768]; /* 3MB Shadow Buffer in BSS */

/* ── Primitives ────────────────────────────────────────────────────────────── */

void vbe_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_vbe_enabled) return;
    if (x >= g_vbe_width || y >= g_vbe_height) return;
    uint32_t idx = y * g_vbe_width + x;
    g_vbe_shadow[idx] = color;
}

void vbe_clear(uint32_t color) {
    if (!g_vbe_enabled) return;
    uint32_t total = (uint32_t)g_vbe_width * g_vbe_height;
    vbe_stosl(g_vbe_shadow, color, total);
}

void vbe_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!g_vbe_enabled) return;
    for (uint32_t row = y; row < y + height && row < g_vbe_height; row++) {
        uint32_t idx = row * g_vbe_width + x;
        uint32_t count = (x + width <= g_vbe_width) ? width : (g_vbe_width - x);
        vbe_stosl(&g_vbe_shadow[idx], color, count);
    }
}

void vbe_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!g_vbe_enabled) return;
    /* Top and bottom edges */
    for (uint32_t col = x; col < x + width && col < g_vbe_width; col++) {
        vbe_put_pixel(col, y, color);
        vbe_put_pixel(col, y + height - 1, color);
    }
    /* Left and right edges */
    for (uint32_t row = y; row < y + height && row < g_vbe_height; row++) {
        vbe_put_pixel(x, row, color);
        vbe_put_pixel(x + width - 1, row, color);
    }
}

void vbe_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!g_vbe_enabled) return;
    /* Bresenham's line algorithm */
    int dx  =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy  = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sx  = (x0 < x1) ? 1 : -1;
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        vbe_put_pixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ── Font rendering (8x16 bitmap) ─────────────────────────────────────────── */
#include "font8x16.h"

void vbe_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color) {
    if (!g_vbe_enabled) return;
    uint8_t idx = (uint8_t)c;
    const uint8_t *glyph = font8x16[idx];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg_color : bg_color;
            vbe_put_pixel(x + col, y + row, color);
        }
    }
}

void vbe_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg_color, uint32_t bg_color) {
    if (!g_vbe_enabled || !str) return;
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += 16;
        } else {
            vbe_draw_char(cx, y, *str, fg_color, bg_color);
            cx += 8;
        }
        str++;
    }
}

/* =============================================================================
 * Mouse Cursor UI Support (Save/Restore Background)
 * =============================================================================
 */
#include "mouse.h"

#define CURSOR_W 12
#define CURSOR_H 18

/* Simple arrow shape */
static const char *mouse_shape[CURSOR_H] = {
    "100000000000",
    "121000000000",
    "122100000000",
    "122210000000",
    "122221000000",
    "122222100000",
    "122222210000",
    "122222221000",
    "122222222100",
    "122222222210",
    "122222111111",
    "122122100000",
    "121012210000",
    "110012210000",
    "100001221000",
    "000001221000",
    "000000110000",
    "000000000000"
};

void vbe_draw_mouse_cursor(int32_t x, int32_t y) {
    /* No longer used directly, integrated into vbe_refresh_screen */
    (void)x; (void)y;
}

/* Saves the shadow-buffer pixels under the cursor into bg[], then draws
 * the cursor shape onto the shadow buffer in their place. Shared by both
 * the full-copy and dirty-rect paths below so the cursor logic itself
 * only lives in one place. */
static void cursor_paint(int32_t mx, int32_t my, uint32_t *bg) {
    for (int32_t j = 0; j < CURSOR_H; j++) {
        for (int32_t i = 0; i < CURSOR_W; i++) {
            int32_t px = mx + i;
            int32_t py = my + j;
            if (px >= 0 && px < g_vbe_width && py >= 0 && py < g_vbe_height) {
                bg[j * CURSOR_W + i] = g_vbe_shadow[py * g_vbe_width + px];

                char c = mouse_shape[j][i];
                if (c == '1') {
                    g_vbe_shadow[py * g_vbe_width + px] = 0x000000; /* Black */
                } else if (c == '2') {
                    g_vbe_shadow[py * g_vbe_width + px] = 0xFFFFFF; /* White */
                }
            }
        }
    }
}

static void cursor_restore(int32_t mx, int32_t my, const uint32_t *bg) {
    for (int32_t j = 0; j < CURSOR_H; j++) {
        for (int32_t i = 0; i < CURSOR_W; i++) {
            int32_t px = mx + i;
            int32_t py = my + j;
            if (px >= 0 && px < g_vbe_width && py >= 0 && py < g_vbe_height) {
                g_vbe_shadow[py * g_vbe_width + px] = bg[j * CURSOR_W + i];
            }
        }
    }
}

/* Copies shadow[y][x0..x0+w) -> VRAM for a single scanline range, honoring
 * the REAL hardware pitch (g_vbe_pitch, bytes) which may differ from the
 * shadow buffer's tightly-packed pitch (g_vbe_width dwords). */
static void flush_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    extern void vbe_movsl(void *dst, const void *src, uint32_t dwords);

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)g_vbe_width)  w = (int32_t)g_vbe_width  - x;
    if (y + h > (int32_t)g_vbe_height) h = (int32_t)g_vbe_height - y;
    if (w <= 0 || h <= 0) return;

    for (int32_t row = 0; row < h; row++) {
        uint32_t sy = (uint32_t)(y + row);
        uint32_t *src = &g_vbe_shadow[sy * g_vbe_width + (uint32_t)x];
        uint8_t  *dst = (uint8_t *)g_vbe_buffer + sy * g_vbe_pitch;
        
        if (g_vbe_bpp == 32) {
            dst += (uint32_t)x * 4u;
            vbe_movsl(dst, src, (uint32_t)w);
        } else if (g_vbe_bpp == 24) {
            dst += (uint32_t)x * 3u;
            for (int32_t col = 0; col < w; col++) {
                uint32_t color = src[col];
                dst[col * 3 + 0] = (uint8_t)(color & 0xFF);
                dst[col * 3 + 1] = (uint8_t)((color >> 8) & 0xFF);
                dst[col * 3 + 2] = (uint8_t)((color >> 16) & 0xFF);
            }
        }
    }
}

void vbe_refresh_screen(void) {
    if (!g_vbe_enabled) return;

    extern int32_t mouse_x, mouse_y;
    int32_t mx = mouse_x;
    int32_t my = mouse_y;

    /* The cursor moves almost every frame and nothing above this file
     * knows to mark it dirty, so it marks its own old + new footprint
     * here before deciding what to flush. */
    static int32_t prev_mx = -10000, prev_my = -10000;
    dirty_mark(prev_mx, prev_my, CURSOR_W, CURSOR_H);
    dirty_mark(mx, my, CURSOR_W, CURSOR_H);
    prev_mx = mx; prev_my = my;

    uint32_t bg[CURSOR_W * CURSOR_H];

    if (dirty_is_full_screen()) {
        /* Full screen refresh honoring hardware pitch */
        cursor_paint(mx, my, bg);
        flush_rect(0, 0, (int32_t)g_vbe_width, (int32_t)g_vbe_height);
        cursor_restore(mx, my, bg);
        dirty_reset();
        return;
    }

    uint32_t rect_count = dirty_count();
    if (rect_count == 0) {
        /* Nothing changed anywhere -- not even the cursor moved. Do
         * nothing: no cursor redraw, no VRAM writes at all. */
        return;
    }

    cursor_paint(mx, my, bg);
    for (uint32_t i = 0; i < rect_count; i++) {
        dirty_rect_t r;
        if (dirty_get(i, &r)) flush_rect(r.x, r.y, r.w, r.h);
    }
    cursor_restore(mx, my, bg);

    dirty_reset();
}
