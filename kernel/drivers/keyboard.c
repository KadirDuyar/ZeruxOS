/* =============================================================================
 * ZeruX OS — PS/2 Keyboard Driver Implementasyonu
 * File: kernel/drivers/keyboard.c
 * =============================================================================
 *
 * Bu dosya PS/2 klavye sürücüsünü yönetir. Scancode Set 1 kodlarını okur,
 * Shift ve Caps Lock modlayıcı durumunu hesaplar, karakterleri daire tampona (ring buffer)
 * koyar ve seri port ile ekrana anında yansıtır.
 * =============================================================================
 */

#include "keyboard.h"
#include "ports.h"
#include "isr.h"
#include "irq.h"
#include "serial.h"
#include "vbe.h"
#include <stdbool.h>

#define KBD_BUFFER_SIZE 256

static char     kbd_buffer[KBD_BUFFER_SIZE];
static uint16_t kbd_head = 0;
static uint16_t kbd_tail = 0;

static bool shift_pressed    = false;
static bool caps_lock_active = false;
static bool ctrl_pressed     = false;
volatile bool shell_interrupt_requested = false;

/* Scancode Set 1 -> ASCII Tablosu (Normal Keys) */
static const char scancode_ascii_normal[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
     0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
   '*',   0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0
};

/* Scancode Set 1 -> ASCII Tablosu (Shift Keys) */
static const char scancode_ascii_shift[128] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
     0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,
   '*',   0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0
};

/* Tampona Karakter Ekle */
static void buffer_put(char ch) {
    uint16_t next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next_head != kbd_tail) {
        kbd_buffer[kbd_head] = ch;
        kbd_head = next_head;
    }
}

/* =============================================================================
 * keyboard_callback() — IRQ1 Interrupt Handler
 * =============================================================================
 */
static void keyboard_callback(registers_t *regs) {
    (void)regs;

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Key Release (0x80 bit'i 1 olan scancode'lar) */
    if (scancode & 0x80) {
        uint8_t released_code = scancode & 0x7F;
        if (released_code == 0x2A || released_code == 0x36) {
            shift_pressed = false;
        } else if (released_code == 0x1D) {
            ctrl_pressed = false;
        }
        return;
    }

    /* Key Press */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    }
    
    if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    }

    if (scancode == 0x3A) {
        caps_lock_active = !caps_lock_active;
        return;
    }
    
    /* Ctrl+C handler */
    if (ctrl_pressed && scancode == 0x2E) {
        shell_interrupt_requested = true;
        return; /* Don't put 'c' into buffer */
    }

    /* Ctrl+Q handler */
    if (ctrl_pressed && scancode == 0x10) {
        buffer_put(0x11);
        return;
    }

    /* Arrow Keys -> VT100 Escape Sequences */
    if (scancode == 0x48) { buffer_put(27); buffer_put('['); buffer_put('A'); return; } /* Up */
    if (scancode == 0x50) { buffer_put(27); buffer_put('['); buffer_put('B'); return; } /* Down */
    if (scancode == 0x4D) { buffer_put(27); buffer_put('['); buffer_put('C'); return; } /* Right */
    if (scancode == 0x4B) { buffer_put(27); buffer_put('['); buffer_put('D'); return; } /* Left */

    /* ASCII Karakter Dönüşümü */
    char ch = 0;
    if (scancode < 128) {
        bool use_shift = shift_pressed ^ caps_lock_active;
        ch = use_shift ? scancode_ascii_shift[scancode] : scancode_ascii_normal[scancode];
    }

    if (ch != 0) {
        buffer_put(ch);
    }
}

/* =============================================================================
 * keyboard_init() — PS/2 Klavye Sürücüsünü Başlat
 * =============================================================================
 */
void keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    shift_pressed = false;
    caps_lock_active = false;
    ctrl_pressed = false;
    shell_interrupt_requested = false;

    /* IRQ1 (Keyboard) kesmesini IRQ dispatcher'a kaydet */
    irq_install_handler(IRQ1_KEYBOARD, keyboard_callback);

    serial_printf("[KEYBOARD] PS/2 Keyboard Driver initialized (IRQ1 hooked).\n");
}

/* =============================================================================
 * keyboard_getchar() — Tampondan Tuş Oku (Non-blocking)
 * =============================================================================
 */
char keyboard_getchar(void) {
    if (kbd_head == kbd_tail) return 0;

    char ch = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return ch;
}

/* =============================================================================
 * keyboard_has_char() — Tamponda Okunacak Tuş Var mı?
 * =============================================================================
 */
bool keyboard_has_char(void) {
    return kbd_head != kbd_tail;
}

void keyboard_inject_char(char c) {
    kbd_buffer[kbd_head] = c;
    kbd_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
}

/* =============================================================================
 * keyboard_set_leds() — PS/2 Klavye LED'lerini Ayarla
 * =============================================================================
 */
static void kbd_wait(void) {
    /* Wait until input buffer is empty */
    while (inb(0x64) & 2) {}
}

void keyboard_set_leds(bool num_lock, bool caps_lock, bool scroll_lock) {
    uint8_t data = 0;
    if (scroll_lock) data |= (1 << 0);
    if (num_lock)    data |= (1 << 1);
    if (caps_lock)   data |= (1 << 2);

    kbd_wait();
    outb(KEYBOARD_DATA_PORT, 0xED); /* Set LEDs command */
    
    kbd_wait();
    outb(KEYBOARD_DATA_PORT, data); /* Send LED state */
}
