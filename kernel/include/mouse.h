/* =============================================================================
 * ZeruX OS — PS/2 Mouse Driver (IRQ12) Header
 * File: kernel/include/mouse.h
 * =============================================================================
 *
 * 8042 Keyboard Controller AUX port üzerinden PS/2 Fare desteği.
 * Standart 3-byte PS/2 fare paketlerini okur, X/Y koordinatlarını günceller
 * ve VBE üzerinde bir fare imleci çizer (arkaplanı koruyarak).
 * =============================================================================
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

/* Global Fare Durumu */
extern int32_t mouse_x;
extern int32_t mouse_y;
extern bool mouse_left_btn;
extern bool mouse_right_btn;
extern bool mouse_middle_btn;

/* Public API */
void mouse_init(void);

#endif /* MOUSE_H */
