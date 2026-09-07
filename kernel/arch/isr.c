/* =============================================================================
 * ZeruX OS — CPU Exception & Kernel Panic Handler
 * File: kernel/arch/isr.c
 * =============================================================================
 *
 * Bu dosya 0–31 arası CPU exception'larını yakalar, detaylı register dump alır
 * ve `kernel_panic()` fonksiyonu ile sistemi güvenli şekilde durdurur.
 * =============================================================================
 */

#include "isr.h"
#include "idt.h"
#include "serial.h"
#include "vga.h"
#include "cpu.h"
#include "keyboard.h"
#include "task.h"
#include "scheduler.h"
#include <stdbool.h>

/* =============================================================================
 * Exception İsim Tablosu (Const-Correctness: Read-Only Data)
 * =============================================================================
 * `static const char * const` -> hem pointer'ların kendisi hem de işaret ettikleri
 * string'ler sabittir (ROM / .rodata segmentinde saklanır).
 */
static const char * const exception_names[32] = {
    [0]  = "Divide-by-Zero Error (#DE)",
    [1]  = "Debug Exception (#DB)",
    [2]  = "Non-Maskable Interrupt (NMI)",
    [3]  = "Breakpoint Exception (#BP)",
    [4]  = "Overflow Exception (#OF)",
    [5]  = "BOUND Range Exceeded (#BR)",
    [6]  = "Invalid Opcode Exception (#UD)",
    [7]  = "Device Not Available (#NM)",
    [8]  = "Double Fault (#DF)",
    [9]  = "Coprocessor Segment Overrun",
    [10] = "Invalid TSS (#TS)",
    [11] = "Segment Not Present (#NP)",
    [12] = "Stack-Segment Fault (#SS)",
    [13] = "General Protection Fault (#GP)",
    [14] = "Page Fault (#PF)",
    [15] = "Reserved Exception",
    [16] = "x87 Floating-Point Exception (#MF)",
    [17] = "Alignment Check Exception (#AC)",
    [18] = "Machine Check Exception (#MC)",
    [19] = "SIMD Floating-Point Exception (#XM)",
    [20] = "Virtualization Exception (#VE)",
    [21] = "Reserved Exception",
    [22] = "Reserved Exception",
    [23] = "Reserved Exception",
    [24] = "Reserved Exception",
    [25] = "Reserved Exception",
    [26] = "Reserved Exception",
    [27] = "Reserved Exception",
    [28] = "Reserved Exception",
    [29] = "Reserved Exception",
    [30] = "Security Exception",
    [31] = "Reserved Exception"
};

/* =============================================================================
 * Dış Assembly Stub Bildirimleri (isr_asm.asm'de tanımlıdır)
 * =============================================================================
 */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

typedef void (*isr_stub_t)(void);

static const isr_stub_t isr_stubs[32] = {
    isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
    isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
    isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
    isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
    isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
};

/* =============================================================================
 * isr_init() — Tüm CPU Exception Kapılarını IDT'ye Kaydet
 * =============================================================================
 */
void isr_init(void) {
    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, (uint32_t)isr_stubs[i], 0x08, IDT_FLAGS_KERNEL_INT);
    }
    serial_printf("[ISR] Registered CPU Exception gates 0..31\n");
}

/* =============================================================================
 * kernel_panic() — Birleşik Kernel Panik & Register Dump Mekanizması
 * =============================================================================
 */
void kernel_panic(const char *reason, registers_t *regs) {
    /* 1. Tüm donanım interrupt'larını anında kapat */
    cpu_cli();

    /* 2. Seri Port'a Detaylı Panic Logu Yaz */
    serial_printf("\n");
    serial_printf("=======================================================\n");
    serial_printf(" !!! KERNEL PANIC: %s !!!\n", reason ? reason : "Unknown Error");
    serial_printf("=======================================================\n");

    if (regs) {
        serial_printf("[FAULT] Vector      : %u (0x%x)\n", regs->int_no, regs->int_no);
        serial_printf("[FAULT] Error Code  : 0x%x (%u)\n", regs->err_code, regs->err_code);
        serial_printf("[FAULT] Instruction : EIP=%p  CS=0x%x\n", (void*)regs->eip, regs->cs);
        serial_printf("[FAULT] Flags       : EFLAGS=%b\n", regs->eflags);
        serial_printf("[REGS]  EAX=0x%x  EBX=0x%x  ECX=0x%x  EDX=0x%x\n",
                      regs->eax, regs->ebx, regs->ecx, regs->edx);
        serial_printf("[REGS]  ESI=0x%x  EDI=0x%x  EBP=0x%x  ESP=0x%x\n",
                      regs->esi, regs->edi, regs->ebp, regs->esp);

        /* Intel SDM Vol.3A §6.12.1: SS ve UserESP sadece Ring 3 -> Ring 0 geçişinde CPU tarafından push edilir */
        if ((regs->cs & 0x03) == 3) {
            serial_printf("[SEGS]  DS=0x%x   SS=0x%x   UserESP=0x%x\n", regs->ds, regs->ss, regs->useresp);
        } else {
            serial_printf("[SEGS]  DS=0x%x   SS=0x10 (Kernel SS)\n", regs->ds);
        }
        serial_printf("[CONTROL] CR0       : 0x%x\n", cpu_read_cr0());

        /* ── Sadece Page Fault (#PF, Vektör 14) Durumunda CR2 Okuma & Decode ── */
        if (regs->int_no == 14) {
            uint32_t cr2_fault_addr;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2_fault_addr));

            serial_printf("[PAGE FAULT] CR2 (Linear Address) : %p\n", (void*)cr2_fault_addr);
            serial_printf("[PAGE FAULT] Cause Details       :\n");
            serial_printf("  - Cause  : %s\n", (regs->err_code & 1) ? "Page-level protection violation" : "Page not present");
            serial_printf("  - Access : %s\n", (regs->err_code & 2) ? "Write operation" : "Read operation");
            serial_printf("  - Mode   : %s\n", (regs->err_code & 4) ? "User Mode (Ring 3)" : "Supervisor Mode (Ring 0)");
            serial_printf("  - Rsvd   : %s\n", (regs->err_code & 8) ? "Reserved bit overwrite detected" : "No reserved bit violation");
            serial_printf("  - Fetch  : %s\n", (regs->err_code & 16) ? "Instruction fetch (NX violation)" : "Data access");
        }
    } else {
        serial_printf("[PANIC] No register frame available.\n");
    }

    serial_printf("=======================================================\n");
    serial_printf("[SYSTEM HALTED] Execution suspended permanently.\n\n");

    /* 3. VGA Ekranına Kırmızı Panik Paneli Çiz */
    vga_init(COLOR_WHITE, COLOR_RED);
    vga_puts_centered(2, "  ! ! !   K E R N E L   P A N I C   ! ! !  ", vga_attr(COLOR_YELLOW, COLOR_RED));
    vga_puts_centered(4, reason ? reason : "Unknown Fatal Error", vga_attr(COLOR_WHITE, COLOR_RED));

    if (regs) {
        char info_buf[80];
        vga_puts_centered(7, "The system has been halted to prevent damage.", vga_attr(COLOR_LIGHT_GREY, COLOR_RED));
        vga_puts_centered(10, "--- Exception Details ---", vga_attr(COLOR_YELLOW, COLOR_RED));

        /* EIP Basımı */
        vga_puts_at(15, 12, "Fault EIP  : ", vga_attr(COLOR_WHITE, COLOR_RED));
        vga_puts_at(15, 13, "Vector #   : ", vga_attr(COLOR_WHITE, COLOR_RED));
        vga_puts_at(15, 14, "Error Code : ", vga_attr(COLOR_WHITE, COLOR_RED));

        /* EIP hex adresi manuel yerleştirme */
        vga_puts_at(30, 12, "0x", vga_attr(COLOR_LIGHT_CYAN, COLOR_RED));
        // Simple hex print in VGA for EIP
        const char *hex_digits = "0123456789abcdef";
        info_buf[8] = '\0';
        uint32_t val = regs->eip;
        for (int i = 7; i >= 0; i--) {
            info_buf[i] = hex_digits[val & 0xF];
            val >>= 4;
        }
        vga_puts_at(32, 12, info_buf, vga_attr(COLOR_LIGHT_CYAN, COLOR_RED));
    }

    vga_puts_centered(22, "Check COM1 Serial Port for complete Register Dump", vga_attr(COLOR_YELLOW, COLOR_RED));

    /* 4. İşlemciyi kalıcı olarak askıya al, klavye LED'lerini flaşla */
    bool led_state = false;
    while (1) {
        keyboard_set_leds(led_state, led_state, led_state);
        led_state = !led_state;
        for (volatile int i = 0; i < 20000000; i++) {} /* Delay */
    }
}

/* =============================================================================
 * isr_handler() — Ortak Assembly Stub'tan Çağrılan C Dispatcher
 * =============================================================================
 */
void isr_handler(registers_t *regs) {
    if (!regs) return;

    const char *reason = "Unknown Exception";
    if (regs->int_no < 32 && exception_names[regs->int_no]) {
        reason = exception_names[regs->int_no];
    }

    /* Check if the exception originated from Ring 3 (User Mode) */
    if ((regs->cs & 3) == 3) {
        uint32_t cr2 = 0;
        if (regs->int_no == 14) {
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        }
        serial_printf("\n=======================================================\n");
        serial_printf("[FAULT] User Mode Exception %u: %s\n", regs->int_no, reason);
        serial_printf("[FAULT] EIP: 0x%x, Error Code: 0x%x, CR2: 0x%x\n", regs->eip, regs->err_code, cr2);
        serial_printf("[FAULT] EAX: 0x%x, EBX: 0x%x, ECX: 0x%x, EDX: 0x%x\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
        serial_printf("[FAULT] ESP: 0x%x, EBP: 0x%x\n", regs->useresp, regs->ebp);
        serial_printf("[FAULT] Terminating offending process...\n");
        serial_printf("=======================================================\n");
        task_exit(-1);
        schedule();
        return; /* Should not reach here if schedule() succeeds, but just in case */
    }

    /* If it originated from Ring 0 (Kernel), panic the whole system */
    kernel_panic(reason, regs);
}
