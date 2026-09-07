/* =============================================================================
 * ZeruX OS — User Mode (Ring 0 -> Ring 3) Switch Header
 * File: kernel/include/usermode.h
 * =============================================================================
 *
 * x86 Protected Mode Ring 0 (Kernel) seviyesinden Ring 3 (User Mode) seviyesine
 * donanımsal `iret` stack hilesi ile geçiş primitives.
 * =============================================================================
 */

#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/* Public API Bildirimleri */
void enter_usermode(void (*entry_point)(void), uint32_t user_stack_top);
void test_usermode_switch(void);
void run_usermode_app_demo(void);

#endif /* USERMODE_H */
