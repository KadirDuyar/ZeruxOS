/* =============================================================================
 * ZeruX OS — VGA Text Mode Driver
 * File: include/vga.h
 * =============================================================================
 *
 * VGA text mode overview
 * ──────────────────────
 * In VGA text mode 3 (80×25), the screen is memory-mapped starting at
 * physical address 0xB8000. The CPU writes directly to this region and
 * the VGA adapter reads it and displays characters on screen — no GPU
 * driver, no framebuffer, no operating system font loader needed.
 *
 * Memory layout of one screen cell (2 bytes per cell):
 *
 *   Offset +0 (low byte)  : ASCII character code (0x20 = space, 0x41 = 'A' …)
 *   Offset +1 (high byte) : Attribute byte
 *                           ┌───────────────────────────────────────────────┐
 *                           │  Bit 7  │  Bits 6:4  │      Bits 3:0         │
 *                           │ Blink * │ Background │     Foreground        │
 *                           └───────────────────────────────────────────────┘
 *                           * Blink bit may also be used as bright-background
 *                             depending on BIOS/hardware configuration.
 *
 * Example — white text on blue background:
 *   foreground = 0xF (white), background = 0x1 (blue)
 *   attribute  = (0x1 << 4) | 0xF = 0x1F
 *
 * Total buffer size:
 *   80 cols × 25 rows × 2 bytes = 4 000 bytes  (less than 4 KB)
 *
 * All functions in this header are declared `static inline` so they are
 * inlined directly into kernel.c with no separate compilation unit needed.
 * =============================================================================
 */

#ifndef VGA_H
#define VGA_H

/* ─── Primitive types (freestanding — no stdlib) ─────────────────────────── */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef int            int32_t;

/* ─── VGA hardware constants ─────────────────────────────────────────────── */
#define VGA_BASE  ((volatile uint16_t *)0xB8000)  /* Physical address of VGA buffer  */
#define VGA_COLS  80                               /* Characters per row              */
#define VGA_ROWS  25                               /* Rows on screen                  */

/* ─── VGA 16-color palette ───────────────────────────────────────────────── */
/*
 * Both foreground (text) and background accept one of these 16 values.
 * These map directly to the 4-bit CGA color indices used inside the
 * attribute byte.
 */
typedef enum {
    COLOR_BLACK         = 0,   /* 0000 */
    COLOR_BLUE          = 1,   /* 0001  ← our background (classic BSOD blue) */
    COLOR_GREEN         = 2,   /* 0010 */
    COLOR_CYAN          = 3,   /* 0011 */
    COLOR_RED           = 4,   /* 0100 */
    COLOR_MAGENTA       = 5,   /* 0101 */
    COLOR_BROWN         = 6,   /* 0110 */
    COLOR_LIGHT_GREY    = 7,   /* 0111 */
    COLOR_DARK_GREY     = 8,   /* 1000 */
    COLOR_LIGHT_BLUE    = 9,   /* 1001 */
    COLOR_LIGHT_GREEN   = 10,  /* 1010 */
    COLOR_LIGHT_CYAN    = 11,  /* 1011 */
    COLOR_LIGHT_RED     = 12,  /* 1100 */
    COLOR_LIGHT_MAGENTA = 13,  /* 1101 */
    COLOR_YELLOW        = 14,  /* 1110 */
    COLOR_WHITE         = 15,  /* 1111  ← our foreground */
} vga_color_t;

/* ─── Attribute / cell builders ─────────────────────────────────────────── */

/*
 * vga_attr() — pack foreground + background into one attribute byte.
 *
 *   Bits [7:4] = background color (4 bits)
 *   Bits [3:0] = foreground color (4 bits)
 *
 * Example:
 *   vga_attr(COLOR_WHITE, COLOR_BLUE)
 *   = (1 << 4) | 15
 *   = 0x10    | 0x0F
 *   = 0x1F
 */
static inline uint8_t vga_attr(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(((uint8_t)bg << 4) | ((uint8_t)fg & 0x0F));
}

/*
 * vga_cell() — pack an ASCII character + attribute into a 16-bit VGA word.
 *
 *   Bits [15:8] = attribute byte (colors)
 *   Bits  [7:0] = ASCII character
 *
 * Writing this 16-bit value to VGA_BASE[row * 80 + col] puts the character
 * on screen at (col, row) with the specified colors.
 */
static inline uint16_t vga_cell(unsigned char ch, uint8_t attr) {
    return (uint16_t)((uint16_t)ch | ((uint16_t)attr << 8));
}

/* ─── Cursor state ──────────────────────────────────────────────────────── */
/*
 * Software cursor — tracks where the next vga_putchar() call will write.
 * (We do not use the hardware VGA cursor registers to keep things simple.)
 *
 * These are file-scope statics, so they are private to kernel.c.
 * Safe for a single-translation-unit kernel (the only .c file we compile).
 */
static int     vga_row          = 0;
static int     vga_col          = 0;
static uint8_t vga_default_attr = 0;

/* ─── Public API ─────────────────────────────────────────────────────────── */

/*
 * vga_init() — Initialize the VGA driver.
 *
 * Sets the default text color (fg on bg) and clears every character cell
 * by filling the entire 80×25 buffer with blue-background space characters.
 * Call this once at the start of kernel_main().
 */
static inline void vga_init(vga_color_t fg, vga_color_t bg) {
    vga_default_attr = vga_attr(fg, bg);
    vga_row = 0;
    vga_col = 0;

    volatile uint16_t *buf = VGA_BASE;
    uint16_t blank = vga_cell(' ', vga_default_attr);

    /* Fill all 2 000 cells (80 × 25) with a blank space character */
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) {
        buf[i] = blank;
    }
}

/*
 * vga_putchar() — Write one character at the current cursor position.
 *
 * Handles:
 *   '\n' — move to start of next row (newline)
 *   '\r' — move to column 0 (carriage return)
 *   Other — write character and advance column
 *
 * If the cursor moves past column 79, it wraps to the next row.
 * If the cursor moves past row 24, the screen scrolls up by one row.
 */
static inline void vga_putchar(char c) {
    volatile uint16_t *buf = VGA_BASE;

    if (c == '\n') {
        /* Newline: reset column, advance row */
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        /* Carriage return: reset column only */
        vga_col = 0;
    } else {
        /* Normal character: write to buffer and advance */
        buf[vga_row * VGA_COLS + vga_col] = vga_cell((unsigned char)c, vga_default_attr);
        vga_col++;

        if (vga_col >= VGA_COLS) {   /* Wrap at right edge */
            vga_col = 0;
            vga_row++;
        }
    }

    /* ── Scroll if we go past the bottom row ── */
    if (vga_row >= VGA_ROWS) {
        /*
         * Shift every row N → row N-1  (copy the cell word by word).
         * This is equivalent to a memmove but without any library.
         */
        for (int r = 0; r < VGA_ROWS - 1; r++) {
            for (int c = 0; c < VGA_COLS; c++) {
                buf[r * VGA_COLS + c] = buf[(r + 1) * VGA_COLS + c];
            }
        }
        /* Clear the newly exposed bottom row */
        uint16_t blank = vga_cell(' ', vga_default_attr);
        for (int c = 0; c < VGA_COLS; c++) {
            buf[(VGA_ROWS - 1) * VGA_COLS + c] = blank;
        }
        vga_row = VGA_ROWS - 1;   /* Cursor stays on the last row */
    }
}

/*
 * vga_puts() — Write a null-terminated string at the current cursor position.
 *
 * Iterates over each character and calls vga_putchar() — newlines and
 * wrapping are handled automatically.
 */
static inline void vga_puts(const char *s) {
    while (*s) {
        vga_putchar(*s++);
    }
}

/*
 * vga_put_at() — Write one character directly at (col, row) with a custom attribute.
 *
 * Does NOT update the software cursor (vga_row / vga_col).
 * Used for drawing borders, UI elements, and decorative text that should
 * not affect where the next vga_puts() call starts writing.
 *
 * Parameters:
 *   col  — column index  [0 … VGA_COLS-1]
 *   row  — row    index  [0 … VGA_ROWS-1]
 *   c    — ASCII character to display
 *   attr — pre-built attribute byte (use vga_attr() to construct)
 */
static inline void vga_put_at(int col, int row, char c, uint8_t attr) {
    if (col < 0 || col >= VGA_COLS) return;   /* Bounds check */
    if (row < 0 || row >= VGA_ROWS) return;
    VGA_BASE[row * VGA_COLS + col] = vga_cell((unsigned char)c, attr);
}

/*
 * vga_puts_at() — Write a null-terminated string starting at (col, row).
 *
 * Does NOT update the software cursor.
 * Stops at the right edge of the screen (no wrapping).
 */
static inline void vga_puts_at(int col, int row, const char *s, uint8_t attr) {
    while (*s && col < VGA_COLS) {
        vga_put_at(col++, row, *s++, attr);
    }
}

/*
 * vga_puts_centered() — Write a string centered on a given row.
 *
 * Calculates the starting column so the string appears in the middle.
 * Does NOT update the software cursor.
 *
 * Parameters:
 *   row  — which screen row to write on
 *   s    — null-terminated string to center
 *   attr — color attribute byte
 */
static inline void vga_puts_centered(int row, const char *s, uint8_t attr) {
    /* Compute string length manually (no strlen in freestanding C) */
    int len = 0;
    const char *p = s;
    while (*p++) len++;

    int col = (VGA_COLS - len) / 2;
    if (col < 0) col = 0;   /* Clamp — very long strings start at col 0 */

    vga_puts_at(col, row, s, attr);
}

/*
 * vga_fill_row() — Fill an entire row with a character and attribute.
 *
 * Useful for drawing horizontal separator lines or highlight bars.
 */
static inline void vga_fill_row(int row, char c, uint8_t attr) {
    for (int col = 0; col < VGA_COLS; col++) {
        vga_put_at(col, row, c, attr);
    }
}

#endif /* VGA_H */
