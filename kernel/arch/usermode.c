/* =============================================================================
 * ZeruX OS — User Mode (Ring 3) Transition Driver
 * File: kernel/arch/usermode.c
 * =============================================================================
 *
 * `enter_usermode` fonksiyonu `iret` stack hilesi ile işlemcinin ayrıcalık
 * seviyesini Ring 0 (Kernel) -> Ring 3 (User Mode) seviyesine düşürür.
 * =============================================================================
 */

#include "usermode.h"
#include "kheap.h"
#include "serial.h"
#include "cpu.h"

extern void enter_usermode_asm(uint32_t entry_point, uint32_t user_stack_top);

void enter_usermode(void (*entry_point)(void), uint32_t user_stack_top) {
    if (!entry_point || !user_stack_top) return;
    enter_usermode_asm((uint32_t)entry_point, user_stack_top);
}

/* =============================================================================
 * Test User Program & Ring 3 Security Violation Harness (Adım 3.3.5)
 * =============================================================================
 */
static void user_program_entry(void) {
    uint16_t cs_val = 0, ds_val = 0;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs_val));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds_val));

    serial_printf("\n");
    serial_printf("  [USER MODE] Successfully entered Ring 3 (User Mode)!\n");
    serial_printf("  [USER MODE] Code Segment (CS) : 0x%x (Expected: 0x1B / Ring 3)\n", cs_val);
    serial_printf("  [USER MODE] Data Segment (DS) : 0x%x (Expected: 0x23 / Ring 3)\n", ds_val);

    if ((cs_val & 3) == 3 && (ds_val & 3) == 3) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [USERMODE TEST PASSED] CPU Privilege Level Is Ring 3!\n");
        serial_printf("   - CS Selector : 0x1B (DPL=3 User Code)\n");
        serial_printf("   - DS Selector : 0x23 (DPL=3 User Data)\n");
        serial_printf("=======================================================\n\n");
    }

    serial_printf("  [USER MODE TEST] Attempting privileged 'cli' instruction in Ring 3...\n");
    serial_printf("  [USER MODE TEST] Expecting General Protection Fault (#GP / ISR 13)...\n\n");

    /* Kasıtlı Ayrıcalıklı Komut İhlali: Ring 3'te CLI çalıştırma */
    __asm__ volatile ("cli");

    /* Eğer buraya ulaşılırsa koruma çalışmamıştır */
    serial_printf("[USER MODE ERROR] Privileged instruction did NOT trigger #GP!\n");
    while (1) {}
}

void test_usermode_switch(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Ring 0 -> Ring 3 (User Mode) Switch\n");
    serial_printf("===========================================\n");

    /* 4 KB User Stack Ayır (kmalloc) */
    void *user_stack = kmalloc(4096);
    uint32_t user_stack_top = (uint32_t)user_stack + 4096;
    user_stack_top &= ~0x07; /* 8-byte alignment */

    serial_printf("[USERMODE] Allocated 4KB User Stack at %p - %p\n", user_stack, (void*)user_stack_top);
    serial_printf("[USERMODE] Executing IRET stack frame transition to entry point %p...\n", user_program_entry);

    enter_usermode(user_program_entry, user_stack_top);
}
