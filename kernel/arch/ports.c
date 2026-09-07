/* =============================================================================
 * ZeruX OS — x86 I/O Port Non-Inline Implementasyonlar
 * File: kernel/arch/ports.c
 * =============================================================================
 *
 * BU DOSYA NE İÇİN?
 * ─────────────────
 * ports.h'daki static inline sürümler optimal performans için yeterlidir.
 * Ancak bazı senaryolarda non-inline fonksiyon sürümlerine ihtiyaç duyulur:
 *
 *   1. Function Pointer Kullanımı
 *      Örnek: bir driver framework'ü port erişim fonksiyonunu çalışma
 *      zamanında seçmek isteyebilir (gerçek donanım vs. simülasyon).
 *      struct { void (*write)(uint16_t, uint8_t); } port_ops;
 *      Static inline fonksiyonların adresi alınamaz.
 *
 *   2. Test Harness / Mocking
 *      Unit test sırasında port erişimlerini intercept etmek için
 *      non-inline sürümleri stub'la değiştirebilirsin.
 *
 *   3. Gelecekte IOPL Kontrolü
 *      User-mode driver desteği eklendiğinde (IOPL, TSS I/O permission bitmap)
 *      buradaki fonksiyonlara ring-level kontrol eklenebilir.
 *
 *   4. Port Erişim Loglama
 *      Debug build'de her port okuma/yazmasını seri porta loglayabilirsin.
 *
 * _p SUFFIX NE DEMEK?
 * ───────────────────
 * "port with delay" — her I/O işleminden sonra io_wait() çağırır.
 * Linux kernel'daki outb_p / inb_p ile aynı mantık.
 *
 * Hangi donanımlar _p gerektirir?
 *   - 8259 PIC (init sequence)
 *   - 8253/8254 PIT (init sequence)
 *   - PS/2 controller (port 0x64/0x60, bazı durumlarda)
 *   - ISA bus'a bağlı eski donanımlar
 *
 * Modern PCIe donanımlar ve MMIO-mapped register'lar için _p gerekmez.
 * =============================================================================
 */

#include "ports.h"   /* inline sürümler + outb_p/inb_p declarasyonları */

/* =============================================================================
 * outb_p — 1 byte yaz + io_wait() gecikme
 * =============================================================================
 * 8259 PIC veya 8253 PIT gibi yavaş ISA donanımlarına yazmak için kullanılır.
 * io_wait() yaklaşık 1–4 mikrosaniye gecikme sağlar.
 */
void outb_p(uint16_t port, uint8_t val) {
    outb(port, val);   /* asıl I/O yazma işlemi */
    io_wait();         /* ~1-4 μs gecikme: donanımın durumu sindirmesi için */
}

/* =============================================================================
 * inb_p — 1 byte oku + io_wait() gecikme
 * =============================================================================
 */
uint8_t inb_p(uint16_t port) {
    uint8_t val = inb(port);   /* asıl I/O okuma işlemi */
    io_wait();                  /* gecikme */
    return val;
}

/* =============================================================================
 * outw_p — 2 byte yaz + io_wait() gecikme
 * =============================================================================
 */
void outw_p(uint16_t port, uint16_t val) {
    outw(port, val);
    io_wait();
}

/* =============================================================================
 * inw_p — 2 byte oku + io_wait() gecikme
 * =============================================================================
 */
uint16_t inw_p(uint16_t port) {
    uint16_t val = inw(port);
    io_wait();
    return val;
}
