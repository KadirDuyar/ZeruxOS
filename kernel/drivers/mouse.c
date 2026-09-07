/* =============================================================================
 * ZeruX OS — PS/2 Mouse Driver (IRQ12) Implementation
 * File: kernel/drivers/mouse.c
 * =============================================================================
 */

#include "mouse.h"
#include "isr.h"
#include "irq.h"
#include "ports.h"
#include "vbe.h"
#include "serial.h"
#include "gui.h"

int32_t mouse_x = 512;
int32_t mouse_y = 384;
bool mouse_left_btn = false;
bool mouse_right_btn = false;
bool mouse_middle_btn = false;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

/* Wait for 8042 buffer to be ready */
static inline int mouse_wait(uint8_t type) {
    uint32_t time_out = 100000; /* ~100ms timeout, avoids multi-minute hang if no PS/2 mouse */
    if (type == 0) {
        while (time_out--) {
            if ((inb(0x64) & 1) == 1) return 0;
        }
    } else {
        while (time_out--) {
            if ((inb(0x64) & 2) == 0) return 0;
        }
    }
    return -1;
}

static inline void mouse_write(uint8_t write) {
    if (mouse_wait(1) != 0) return;
    outb(0x64, 0xD4);
    if (mouse_wait(1) != 0) return;
    outb(0x60, write);
}

static inline uint8_t mouse_read(void) {
    if (mouse_wait(0) != 0) return 0xFF;
    return inb(0x60);
}

extern bool g_usb_mouse_active;

static void mouse_callback(registers_t *regs) {
    (void)regs;

    uint8_t status = inb(0x64);
    if (!(status & 1)) return; /* No data in buffer */

    uint8_t byte = inb(0x60);
    
    if (g_usb_mouse_active) {
        return; /* Drop PS/2 bytes if USB mouse is attached */
    }

    switch (mouse_cycle) {
        case 0:
            /* Bit 3 must be 1 (sync bit). Bit 6 and 7 (overflow) must be 0 */
            if ((byte & 0x08) == 0 || (byte & 0xC0) != 0) return;
            mouse_packet[0] = byte;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_packet[1] = byte;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_packet[2] = byte;
            mouse_cycle = 0;

            /* Discard if overflow bit 6 or 7 was set in byte 0 */
            if (mouse_packet[0] & 0xC0) return;

            /* Parse 9-bit relative movement with sign extension */
            int32_t dx = (int32_t)mouse_packet[1];
            int32_t dy = (int32_t)mouse_packet[2];
            if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;
            if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;

            mouse_left_btn   = (mouse_packet[0] & 0x01) != 0;
            mouse_right_btn  = (mouse_packet[0] & 0x02) != 0;
            mouse_middle_btn = (mouse_packet[0] & 0x04) != 0;

            mouse_x += dx;
            mouse_y -= dy; /* PS/2 Y is positive upwards, screen is downwards */

            /* Clamp to screen boundaries */
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int32_t)g_vbe_width)  mouse_x = (int32_t)g_vbe_width - 1;
            if (mouse_y >= (int32_t)g_vbe_height) mouse_y = (int32_t)g_vbe_height - 1;
            break;
    }
}

void mouse_init(void) {
    /* 1. Flush any existing data in the controller output buffer */
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    /* 2. Enable Auxiliary Device (Mouse) */
    mouse_wait(1);
    outb(0x64, 0xA8);

    /* 3. Read Controller Command Byte */
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60);

    /* 4. Enable IRQ1 (bit 0) & IRQ12 (bit 1), enable clocks (clear bit 4 and 5) */
    status |= (1 << 0) | (1 << 1);
    status &= ~((1 << 4) | (1 << 5));

    /* 5. Write back Controller Command Byte */
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    /* 6. Set defaults (0xF6) - DO NOT send 0xFF reset as it breaks BIOS USB legacy emulation! */
    mouse_write(0xF6);
    mouse_read(); /* Wait ACK (0xFA) */

    /* 7. Enable streaming / data reporting (0xF4) */
    mouse_write(0xF4);
    mouse_read(); /* Wait ACK (0xFA) */

    /* 8. Drain any leftover bytes before installing handler to ensure clean synchronization */
    while (inb(0x64) & 1) {
        inb(0x60);
    }
    mouse_cycle = 0;

    /* 9. Hook IRQ12 on PIC */
    irq_install_handler(IRQ12_MOUSE, mouse_callback);

    serial_printf("[MOUSE] Mouse Driver initialized (IRQ12 hooked, streaming enabled).\n");
}
