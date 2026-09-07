/* =============================================================================
 * ZeruX OS — COM1 UART Serial Port Driver API
 * File: kernel/include/serial.h
 * =============================================================================
 *
 * UART (Universal Asynchronous Receiver/Transmitter) nedir?
 * ──────────────────────────────────────────────────────────
 * UART, asenkron seri iletişim protokolü implementasyonudur. x86 PC'lerde
 * tarihsel olarak RS-232 fiziksel arayüzüyle birlikte kullanılır.
 *
 * KERNEL GELİŞTİRMEDE NEDEN KRİTİK?
 *   - VGA driver çalışmadan önce bile debug çıktısı alınabilir
 *   - Kernel panic sırasında (VGA bozuk olsa bile) mesaj gönderilebilir
 *   - QEMU: `qemu-system-i386 -serial stdio` ile terminal'e akar
 *   - QEMU: `qemu-system-i386 -serial file:debug.log` ile dosyaya loglanır
 *   - Gerçek donanım: minicom/picocom ile RS-232 kablosu üzerinden okunur
 *
 * UART 16550A hakkında:
 *   - PC'lerin standart UART chip'i. FIFO (First-In-First-Out) buffer içerir.
 *   - 8 adet I/O register, 8-byte aralığında (base + 0 ile base + 7)
 *   - COM1: base port 0x3F8, IRQ4
 *   - COM2: base port 0x2F8, IRQ3
 *
 * Bu driver tek-yönlü (TX-only) çalışır — sadece veri gönderir.
 * IRQ'lar devre dışıdır; polling modunda çalışır.
 * Bu tasarım IDT ve PIC kurulumu olmadan da çalışmasını sağlar.
 * =============================================================================
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>   /* uint8_t, uint16_t, uint32_t */
#include <stdarg.h>   /* va_list, va_start, va_end, va_arg — freestanding'de mevcut */

/* =============================================================================
 * COM Port I/O Base Adresleri
 * =============================================================================
 * IBM PC standardı: her COM port için 8 register, 8-byte I/O aralığı.
 * Bu adresler PC BIOS'u tarafından çok eski dönemden beri rezerve edilmiştir.
 */
#define SERIAL_COM1_BASE   0x3F8U   /* COM1 — IRQ4 */
#define SERIAL_COM2_BASE   0x2F8U   /* COM2 — IRQ3 */
#define SERIAL_COM3_BASE   0x3E8U   /* COM3 — IRQ4 (COM1 ile paylaşır) */
#define SERIAL_COM4_BASE   0x2E8U   /* COM4 — IRQ3 (COM2 ile paylaşır) */

/* =============================================================================
 * Baud Rate Divisor Değerleri
 * =============================================================================
 * UART clock frekansı: 1.8432 MHz (115200 × 16)
 * Baud rate = 115200 / Divisor
 *
 * divisor = 115200 / istenilen_baud_rate
 * Örnek: 38400 baud → divisor = 115200 / 38400 = 3
 *
 * Yüksek baud rate = hızlı ama gürültüye hassas
 * QEMU'da hız fark etmez — her ikisi de anlık çalışır.
 */
#define SERIAL_BAUD_115200  1U    /* En hızlı */
#define SERIAL_BAUD_57600   2U
#define SERIAL_BAUD_38400   3U   /* İyi bir denge — bu driver'ın default'u */
#define SERIAL_BAUD_9600    12U  /* Klasik "yavaş modem" hızı */

/* =============================================================================
 * Dönüş Kodları
 * =============================================================================
 */
#define SERIAL_OK           0     /* İşlem başarılı */
#define SERIAL_ERR_LOOPBACK (-1)  /* Loopback test başarısız — UART bulunamadı */

/* =============================================================================
 * Public API
 * =============================================================================
 */

/**
 * serial_init() — COM1 UART'ı başlatır.
 *
 * Şunları yapar:
 *   1. Tüm interrupt'ları devre dışı bırakır (polling mod)
 *   2. Baud rate'i 38400 olarak ayarlar (divisor = 3)
 *   3. 8N1 formatını ayarlar: 8 data bit, parity yok, 1 stop bit
 *   4. FIFO'yu etkinleştirir ve sıfırlar
 *   5. Modem control register'ını ayarlar (DTR, RTS, OUT2)
 *   6. Loopback test yapar — UART'ın gerçekten var olduğunu doğrular
 *   7. Normal operasyon moduna geçer
 *
 * @return SERIAL_OK başarılı, SERIAL_ERR_LOOPBACK loopback test başarısız
 *
 * ÇAĞRI: kernel_main()'in en başında, VGA init'ten önce çağrılmalıdır.
 * Böylece VGA başlamadan önce bile debug mesajları alınabilir.
 */
int serial_init(void);

/**
 * serial_putchar() — Tek bir ASCII karakteri gönderir.
 *
 * '\n' → '\r\n' dönüşümü yapar. Terminal emülatörleri (QEMU stdio, picocom)
 * sadece '\n' gönderilirse satır başına dönmez ("staircase" efekti oluşur).
 *
 * @param c  Gönderilecek karakter
 */
void serial_putchar(char c);

/**
 * serial_putchar_raw() — Karakteri dönüşüm yapmadan doğrudan gönderir.
 *
 * '\n' → '\r\n' dönüşümü YAPILMAZ.
 * Binary veri veya özel protokol göndermek için kullanılır.
 *
 * @param c  Gönderilecek ham karakter
 */
void serial_putchar_raw(char c);

/**
 * serial_puts() — Null-terminated string gönderir.
 *
 * Her karakter için serial_putchar() çağırır, dolayısıyla '\n' dönüşümü uygulanır.
 * @param s  Null ile sonlanan C string'i (NULL gönderilirse işlem yapılmaz)
 */
void serial_puts(const char *s);

/**
 * serial_printf() — Format string desteğiyle seri port çıktısı.
 *
 * Desteklenen format specifier'lar:
 *   %c   → char (karakter)
 *   %s   → const char * (string, NULL ise "(null)" yazar)
 *   %d   → int32_t  (signed decimal)
 *   %u   → uint32_t (unsigned decimal)
 *   %x   → uint32_t (hex, küçük harf, 8 digit, örn: deadbeef)
 *   %X   → uint32_t (hex, büyük harf, 8 digit, örn: DEADBEEF)
 *   %p   → void *   (pointer: "0x" prefix + 8-digit hex)
 *   %b   → uint32_t (binary: "0b" prefix + 32-bit, '_' ile gruplandırılmış)
 *   %%   → literal '%' karakteri
 *
 * DESTEKLENMEYEN (gelecek sürümler için tasarıma hazır):
 *   Genişlik: %-10s, %08x  → ileride eklenebilir
 *   Precision: %.5f        → floating point ZeruX'ta desteklenmez
 *   %l, %ll modifiers      → 64-bit integer için ileride eklenebilir
 *
 * @param fmt  Format string
 * @param ...  Format string'e karşılık gelen argümanlar
 */
void serial_printf(const char *fmt, ...);

/**
 * serial_has_char() — RX buffer'da okunacak karakter var mı kontrol eder.
 * @return 1 eğer karakter varsa, 0 yoksa
 */
int serial_has_char(void);

/**
 * serial_getchar() — Seri porttan (COM1) tek bir karakter okur.
 * Veri gelene kadar bloke olur (polling).
 * @return Okunan karakter
 */
char serial_getchar(void);
/**
 * serial_set_vbe_mirror() — Set a mirror function to write characters to a graphical terminal
 */
void serial_set_vbe_mirror(void (*fn)(char c));
void serial_set_capture_fn(void (*fn)(char c));
void serial_set_uart_output(int enabled);

#endif /* SERIAL_H */
