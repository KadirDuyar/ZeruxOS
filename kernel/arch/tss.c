/* =============================================================================
 * ZeruX OS — Task State Segment (TSS) Driver Implementasyonu
 * File: kernel/arch/tss.c
 * =============================================================================
 *
 * TSS, x86 mimarisinde User Mode (Ring 3) -> Kernel Mode (Ring 0) kesme ve
 * sistem çağrısı geçişlerinde işlemcinin kullanacağı çekirdek stack'ini (esp0)
 * saklayan 104-baytlık donanımsal yapıdır.
 * =============================================================================
 */

#include "tss.h"
#include "gdt.h"
#include "serial.h"

static tss_entry_t g_tss;

/* Assembly LTR (Load Task Register) stub'ı (gdt_asm.asm'de tanımlı) */
extern void tss_flush(void);

/* =============================================================================
 * tss_init() — TSS Yapısını Sıfırla ve GDT'ye Bağlayıp LTR ile Yükle
 * =============================================================================
 */
void tss_init(uint16_t kernel_ss, uint32_t kernel_esp) {
    uint8_t *ptr = (uint8_t*)&g_tss;
    for (uint32_t i = 0; i < sizeof(tss_entry_t); i++) {
        ptr[i] = 0;
    }

    g_tss.ss0        = kernel_ss;   /* 0x10 = Kernel Data Selector */
    g_tss.esp0       = kernel_esp;  /* Çekirdek stack tepesi */
    g_tss.iomap_base = sizeof(tss_entry_t); /* 104 = I/O Bitmap yok */

    /* GDT Index 5 (0x28) içine TSS Descriptor'ını kaydet */
    uint32_t base  = (uint32_t)&g_tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    /* Access=0x89 (Present=1, DPL=0, Type=9 -> 32-bit TSS Available) */
    gdt_set_gate(5, base, limit, 0x89, 0x00);

    /* Assembly `ltr` instruction ile Task Register (TR) yazmacına 0x28 yükle */
    tss_flush();

    serial_printf("[TSS] TSS Descriptor installed at GDT Index 5 (0x28).\n");
    serial_printf("[TSS] Task Register (TR) loaded via LTR instruction.\n");
}

/* =============================================================================
 * tss_set_kernel_stack() — Context Switch Esnasında Ring 0 Stack'i Günceller
 * =============================================================================
 * Her görev değişiminde (context switch) çağrılarak TSS.esp0 güncellenir.
 * Böylece Ring 3'te çalışan görev bir kesme aldığında CPU bu stack'e zıplar.
 */
void tss_set_kernel_stack(uint32_t esp0) {
    g_tss.esp0 = esp0;
}

/* =============================================================================
 * tss_get_tr() — STR (Store Task Register) Komutu ile Donanımsal TR Değerini Döner
 * =============================================================================
 */
uint16_t tss_get_tr(void) {
    uint16_t tr_val = 0;
    __asm__ volatile ("str %0" : "=r"(tr_val));
    return tr_val;
}

/* =============================================================================
 * dump_tss() — TSS Durumunu Seri Port'a Detaylı Raporlar
 * =============================================================================
 */
void dump_tss(void) {
    serial_printf("-------------------------------------------\n");
    serial_printf("[TSS DUMP] Address: %p  TR Selector: 0x%x\n", (void*)&g_tss, tss_get_tr());
    serial_printf("[TSS] Ring 0 Stack (ESP0) : %p\n", (void*)g_tss.esp0);
    serial_printf("[TSS] Ring 0 Segment (SS0): 0x%x\n", g_tss.ss0);
    serial_printf("[TSS] I/O Map Offset       : %u bytes\n", g_tss.iomap_base);
    serial_printf("-------------------------------------------\n");
}
