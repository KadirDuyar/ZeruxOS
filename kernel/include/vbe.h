/* =============================================================================
 * ZeruX OS — VBE (VESA BIOS Extensions) Linear Framebuffer Driver Header
 * File: kernel/include/vbe.h
 * =============================================================================
 */

#ifndef VBE_H
#define VBE_H

#include <stdint.h>

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01D0

#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

#define VBE_DISPI_ID4               0xB0C4

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_GETCAPS           0x02
#define VBE_DISPI_8BIT_DAC          0x20
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

/* Memory operations for fast VRAM/Shadow buffer copying */
static inline void vbe_stosl(void *dst, uint32_t val, uint32_t count) {
    uint32_t d0, d1;
    asm volatile("cld; rep stosl" : "=&D"(d0), "=&c"(d1) : "0"(dst), "a"(val), "1"(count) : "memory");
}

static inline void vbe_movsl(void *dst, const void *src, uint32_t count) {
    uint32_t d0, d1, d2;
    asm volatile("cld; rep movsl" : "=&D"(d0), "=&S"(d1), "=&c"(d2) : "0"(dst), "1"(src), "2"(count) : "memory");
}

void vbe_init(uint16_t xres, uint16_t yres, uint16_t bpp);
void vbe_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void vbe_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void vbe_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void vbe_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void vbe_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);
void vbe_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg_color, uint32_t bg_color);
void vbe_clear(uint32_t color);

extern uint16_t g_vbe_width;
extern uint16_t g_vbe_height;
extern uint16_t g_vbe_bpp;
extern uint32_t g_vbe_pitch;
extern uint32_t *g_vbe_buffer;
extern uint32_t g_vbe_shadow[1024 * 768];
extern int g_vbe_enabled;

/* Mouse Cursor UI */
void vbe_draw_mouse_cursor(int32_t x, int32_t y);
void vbe_restore_mouse_cursor(int32_t x, int32_t y);
void vbe_hide_mouse(void);
void vbe_show_mouse(void);
void vbe_refresh_screen(void);

#endif /* VBE_H */
