/* =============================================================================
 * ZeruX OS — x86 I/O Port Primitives
 * File: kernel/include/ports.h
 * =============================================================================
 *
 * x86 mimarisinde donanım haberleşmesi iki mekanizmayla yapılır:
 *
 *   1. Memory-Mapped I/O (MMIO)
 *      Donanım register'ları fiziksel bellek adresi olarak haritalanır.
 *      Örnek: VGA text buffer @ 0xB8000, Local APIC @ 0xFEE00000
 *      Erişim: normal MOV instruction'ları (mov al, [0xB8000])
 *
 *   2. Port-Mapped I/O (PMIO)  ← bu dosya bununla ilgilenir
 *      Donanım register'larına 0–65535 arası "I/O port" adresleriyle erişilir.
 *      Erişim: özel IN / OUT instruction'ları
 *      x86'da bu iki adres uzayı tamamen birbirinden ayrıdır.
 *      Örnekler:
 *        0x3F8  → COM1 UART (seri port)
 *        0x20   → Master 8259 PIC command register
 *        0x40   → 8253/8254 PIT Channel 0
 *        0x60   → PS/2 keyboard data register
 *        0x80   → POST diagnostic port (gecikme için kullanılır)
 *
 * Intel SDM Referans: Vol. 1, Chapter 18 — I/O Port Addressing
 *
 * NEDEN STATIC INLINE?
 *   inb/outb gibi tek instruction'a karşılık gelen fonksiyonlar için
 *   function call overhead (push/call/ret) tamamen gereksizdir.
 *   `static inline` ile derleyici çağrıyı doğrudan tek IN/OUT instruction'ına
 *   dönüştürür. Her .c dosyası kendi kopyasını alır (static), ODR ihlali yok.
 *
 * GCC INLINE ASM SYNTAX:
 *   __asm__ volatile ("instruction" : outputs : inputs : clobbers);
 *   volatile → derleyicinin bu asm bloğunu optimize edip kaldırmasını engeller.
 *   "memory"  → derleyiciye bellek sıralamasını (reordering) korumasını söyler.
 * =============================================================================
 */

#ifndef PORTS_H
#define PORTS_H

#include <stdint.h>   /* uint8_t, uint16_t, uint32_t — GCC freestanding'de mevcut */

/* =============================================================================
 * outb — I/O port'a 1 byte (8-bit) yaz
 * =============================================================================
 *
 * x86 Assembly karşılığı: OUT DX, AL
 *   DX = hedef port adresi (16-bit)
 *   AL = yazılacak değer   (8-bit)
 *
 * GCC constraint notasyonu:
 *   "a"(val)  → val değerini AL/AX/EAX register grubuna yükle
 *   "Nd"(port)→ port 0-255 ise 8-bit immediate, 255+ ise DX register'ı kullan
 *               Bu optimizasyon: outb(0x80, 0) → "outb $0, $0x80" (2 byte opcode)
 *               yerine outb(port, val) ile port değişkeni varsa DX'i kullanır.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile (
        "outb %0, %1"        /* x86: OUT AL, DX  (AT&T syntax: src, dst) */
        :                    /* output operand: yok */
        : "a"(val),          /* %0 → AL = val */
          "Nd"(port)         /* %1 → imm8 veya DX = port */
        : "memory"           /* bu asm bellek sıralamasını etkileyebilir */
    );
}

/* =============================================================================
 * inb — I/O port'tan 1 byte (8-bit) oku
 * =============================================================================
 *
 * x86 Assembly karşılığı: IN AL, DX
 *   DX = kaynak port adresi (16-bit)
 *   AL = okunan değer → return value
 *
 * "=a"(val) → output constraint: AL register'ından val değişkenine yaz.
 *             "=" prefix'i bu operandın sadece yazıldığını (output-only) belirtir.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile (
        "inb %1, %0"         /* x86: IN AL, DX */
        : "=a"(val)          /* output: AL → val */
        : "Nd"(port)         /* input:  port → DX */
        : "memory"
    );
    return val;
}

/* =============================================================================
 * outw — I/O port'a 2 byte (16-bit, word) yaz
 * =============================================================================
 * Kullanım alanları: ATA/IDE data register (0x1F0), bazı PCI config portları
 * x86 Assembly: OUT DX, AX
 */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(val), "Nd"(port)
        : "memory"
    );
}

/* =============================================================================
 * inw — I/O port'tan 2 byte (16-bit, word) oku
 * =============================================================================
 * x86 Assembly: IN AX, DX
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile (
        "inw %1, %0"
        : "=a"(val)
        : "Nd"(port)
        : "memory"
    );
    return val;
}

/* =============================================================================
 * outl — I/O port'a 4 byte (32-bit, dword) yaz
 * =============================================================================
 * Kullanım alanları: PCI Configuration Space (CF8h/CFCh), 32-bit IDE/ATA controllers
 * x86 Assembly: OUT DX, EAX
 */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(val), "Nd"(port)
        : "memory"
    );
}

/* =============================================================================
 * inl — I/O port'tan 4 byte (32-bit, dword) oku
 * =============================================================================
 * x86 Assembly: IN EAX, DX
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile (
        "inl %1, %0"
        : "=a"(val)
        : "Nd"(port)
        : "memory"
    );
    return val;
}

/* =============================================================================
 * io_wait — ~1–4 mikrosaniye I/O gecikme
 * =============================================================================
 *
 * PROBLEM: Eski ISA donanımları (8259 PIC, 8253 PIT, PS/2 controller) yavaştır.
 * Modern CPU, bir I/O yazma işlemini nanosaniyeler içinde tamamlayabilir.
 * Eğer hemen ardından okuma yaparsak, donanım henüz hazır olmayabilir.
 *
 * ÇÖZÜM: Port 0x80 (IBM POST diagnostic port) tamamen zararsızdır — hiçbir
 * donanım bu porta yanıt vermez. Buraya yazmak bir I/O bus cycle tüketir ve
 * yaklaşık 1–4 mikrosaniye gecikme sağlar.
 *
 * MODERN DURUM: Modern x86'da I/O bus cycle zaten bir mikrosaniye mertebesinde
 * tamamlanır (IO APIC, PCIe bridge). Ama uyumluluk ve güvenlik için PIC, PIT
 * gibi donanımları init ederken hâlâ kullanılır.
 *
 * Intel SDM Vol.1 §18.10: "An I/O instruction can be used to ensure ordering
 * between two I/O accesses that should not be reordered."
 */
static inline void io_wait(void) {
    outb(0x80, 0x00);   /* POST debug port'una dummy write → ~1-4 μs gecikme */
}

/* =============================================================================
 * _p suffix'li varyantlar — arch/ports.c'de implement edilir
 * =============================================================================
 *
 * "Port-delayed" sürümler: her I/O işleminden sonra io_wait() çağırır.
 * Yavaş ISA donanımlarına yazarken kullanılır.
 *
 * Ne zaman _p kullanmalısın?
 *   - 8259 PIC initialization (PIC_MASTER_CMD, PIC_MASTER_DATA portları)
 *   - 8253/8254 PIT initialization
 *   - PS/2 controller (port 0x64) bazı durumlarda
 *
 * Neden function pointer'a ihtiyaç var?
 *   - Driver framework: port erişim stratejisi çalışma zamanında seçilebilir
 *   - Test harness: port erişimlerini intercept edip test edebilirsin
 *   - Gelecekte: IOPL kontrolü veya port erişim logging buraya eklenebilir
 */
void    outb_p(uint16_t port, uint8_t val);
uint8_t inb_p(uint16_t port);
void    outw_p(uint16_t port, uint16_t val);
uint16_t inw_p(uint16_t port);

#endif /* PORTS_H */
