/* =============================================================================
 * ZeruX OS — PS/2 Keyboard Driver Header
 * File: kernel/include/keyboard.h
 * =============================================================================
 *
 * PS/2 klavye kontrolcüsünden IRQ1 vasıtasıyla gelen scancode'ları yakalar,
 * Scancode Set 1 tablosu ile ASCII karakterlere çevirir.
 * =============================================================================
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* PS/2 Keyboard Port Sabitleri */
#define KEYBOARD_DATA_PORT   0x60  /* Scancode Veri Portu */
#define KEYBOARD_STATUS_PORT 0x64  /* Kontrolcü Durum Portu */

/* Public API Bildirimleri */
void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_char(void);
void keyboard_inject_char(char c);

/* VBE terminal klavye dinleyicisi eklemek için */
typedef void (*keyboard_listener_t)(char);
void keyboard_set_listener(keyboard_listener_t listener);

void keyboard_set_leds(bool num_lock, bool caps_lock, bool scroll_lock);

extern volatile bool shell_interrupt_requested;

#endif /* KEYBOARD_H */
