/* =============================================================================
 * ZeruX OS — COM1 UART Serial Port Driver
 * File: kernel/drivers/serial.c
 * =============================================================================
 *
 * UART 16550A Register Haritası (COM1 base = 0x3F8):
 *
 *  Offset│ DLAB=0 Okuma      │ DLAB=0 Yazma      │ DLAB=1
 *  ──────┼───────────────────┼───────────────────┼──────────────────────────
 *  +0    │ RBR (Rx Buffer)   │ THR (Tx Holding)  │ DLL (Divisor Latch Low)
 *  +1    │ IER (Int Enable)  │ IER (Int Enable)  │ DLH (Divisor Latch High)
 *  +2    │ IIR (Int ID)      │ FCR (FIFO Ctrl)   │ —
 *  +3    │ LCR (Line Ctrl)   │ LCR (Line Ctrl)   │ LCR (bit7=DLAB)
 *  +4    │ MCR (Modem Ctrl)  │ MCR (Modem Ctrl)  │ —
 *  +5    │ LSR (Line Status) │ —                 │ —
 *  +6    │ MSR (Modem Status)│ —                 │ —
 *  +7    │ SCR (Scratch)     │ SCR (Scratch)     │ —
 *
 * DLAB (Divisor Latch Access Bit) = LCR register'ının bit 7'si.
 *   DLAB=1 ayarlandığında +0 ve +1 offset'leri baud rate divisor'ını gösterir.
 *   Bu sayede normal data register'ları ile baud rate register'ları aynı adresi
 *   paylaşır — zekice ama karmaşık bir multiplexing düzeni.
 *
 * BAUD RATE HESABI:
 *   UART internal clock = 1.8432 MHz = 115200 Hz × 16
 *   Baud Rate = 1.8432 MHz / (16 × Divisor) = 115200 / Divisor
 *   38400 baud → Divisor = 115200 / 38400 = 3
 * =============================================================================
 */

#include "serial.h"   /* bu driver'ın public API'si */
#include "ports.h"    /* inb() / outb() I/O port primitifleri */
#include "klog.h"     /* dmesg kernel log ring buffer kancası */

/* VBE terminal mirror: ayarlandığında serial_putchar her karakter için çağrılır */
static void (*s_vbe_mirror_fn)(char c) = 0;

void serial_set_vbe_mirror(void (*fn)(char c)) {
    s_vbe_mirror_fn = fn;
}

/* API capture mirror: ayarlandığında çıktı buraya da gönderilir */
static void (*s_capture_mirror_fn)(char c) = 0;

void serial_set_capture_fn(void (*fn)(char c)) {
    s_capture_mirror_fn = fn;
}

/* =============================================================================
 * UART Register Offset Sabitleri
 * =============================================================================
 * Bu offset'ler COM1 base adresi (0x3F8) ile toplanarak kullanılır.
 * Örnek: COM1 LCR'ye yaz → outb(COM1_BASE + UART_LCR, değer)
 */
#define UART_RBR     0   /* DLAB=0: Receive Buffer Register (okuma) */
#define UART_THR     0   /* DLAB=0: Transmit Holding Register (yazma) */
#define UART_DLL     0   /* DLAB=1: Divisor Latch Low byte */
#define UART_IER     1   /* DLAB=0: Interrupt Enable Register */
#define UART_DLH     1   /* DLAB=1: Divisor Latch High byte */
#define UART_FCR     2   /* FIFO Control Register (write-only) */
#define UART_IIR     2   /* Interrupt Identification Register (read-only) */
#define UART_LCR     3   /* Line Control Register */
#define UART_MCR     4   /* Modem Control Register */
#define UART_LSR     5   /* Line Status Register */
#define UART_MSR     6   /* Modem Status Register */
#define UART_SCR     7   /* Scratch Register (loopback test için kullanılır) */

/* =============================================================================
 * LCR (Line Control Register) Bit Tanımları
 * =============================================================================
 * LCR veri formatını (data bits, stop bits, parity) ve DLAB'ı kontrol eder.
 *
 * Bit 1:0 — Word Length (kaç data bit):
 *   00 = 5 bit  01 = 6 bit  10 = 7 bit  11 = 8 bit (standart → 8N1)
 * Bit 2 — Stop Bits: 0 = 1 stop bit  1 = 2 stop bit
 * Bit 5:3 — Parity: 000 = none  001 = odd  011 = even
 * Bit 6 — Break Enable
 * Bit 7 — DLAB (Divisor Latch Access Bit)
 */
#define LCR_8N1      0x03   /* 8 data bit, no parity, 1 stop bit — evrensel standart */
#define LCR_DLAB     0x80   /* Divisor latch'e erişim için DLAB=1 */

/* =============================================================================
 * FCR (FIFO Control Register) Bit Tanımları
 * =============================================================================
 * FIFO (First-In-First-Out) buffer, burst veri transferlerinde kayıp önler.
 * UART 16550A: 16-byte RX FIFO + 16-byte TX FIFO
 */
#define FCR_ENABLE   0x01   /* FIFO'yu etkinleştir (her iki yön) */
#define FCR_CLR_RX   0x02   /* RX FIFO'yu sıfırla (içeriği sil) */
#define FCR_CLR_TX   0x04   /* TX FIFO'yu sıfırla */
#define FCR_TRIG_14  0xC0   /* RX interrupt eşiği: FIFO 14 byte dolduğunda tetikle */

/* =============================================================================
 * MCR (Modem Control Register) Bit Tanımları
 * =============================================================================
 * RS-232 modem sinyallerini kontrol eder. Loopback test için de kullanılır.
 */
#define MCR_DTR      0x01   /* Data Terminal Ready — "hazırım" sinyali */
#define MCR_RTS      0x02   /* Request to Send — "veri göndermek istiyorum" */
#define MCR_OUT1     0x04   /* OUT1 — uygulama tanımlı çıkış */
#define MCR_OUT2     0x08   /* OUT2 — IRQ enable: bu bit 1 olmadan IRQ üretilmez */
#define MCR_LOOP     0x10   /* Loopback Test: TX → RX'e dahili bağlantı */

/* =============================================================================
 * LSR (Line Status Register) Bit Tanımları
 * =============================================================================
 * Bu register'ı poll ederek verinin hazır olup olmadığını anlarız.
 * Interrupt-driven yerine polling kullanıyoruz (IDT henüz kurulmadı).
 */
#define LSR_DR       0x01   /* Data Ready: RX buffer'da okunacak veri var */
#define LSR_OE       0x02   /* Overrun Error: yeni veri geldi ama eski okunmadı */
#define LSR_PE       0x04   /* Parity Error */
#define LSR_FE       0x08   /* Framing Error: yanlış stop bit */
#define LSR_BI       0x10   /* Break Interrupt */
#define LSR_THRE     0x20   /* Transmit Holding Register Empty: yeni byte gönderilebilir */
#define LSR_TEMT     0x40   /* Transmitter Empty: hem THR hem shift register boş */
#define LSR_FIFOE    0x80   /* FIFO Error */

/* =============================================================================
 * Driver İç Durumu
 * =============================================================================
 * Bu değişkenler sadece bu dosya kapsamında (static) erişilebilir.
 * Gelecekte birden fazla COM port desteklemek için struct'a dönüştürülebilir.
 */
static uint16_t com_base  = SERIAL_COM1_BASE;  /* aktif COM port base adresi */
static int      com_ready = 0;                  /* 1 = init başarılı, 0 = devre dışı */
static int s_uart_output_enabled = 1;           /* 1 = UART çıkışı aktif, 0 = susturulmuş */

/* =============================================================================
 * serial_wait_tx() — THR (Transmit Holding Register) boşalana kadar bekle
 * =============================================================================
 * Polling: LSR'nin THRE bitini sürekli okur.
 * THRE=1 → UART yeni bir byte kabul edebilir durumda.
 *
 * Bu busy-wait döngüsü IDT + PIT kurulmadan önce güvenli tek yaklaşımdır.
 * PIT Timer kurulduktan sonra timeout mekanizması eklenebilir.
 */
static inline void serial_wait_tx(void) {
    /* LSR bit 5 (THRE) set olana kadar döngüde kal */
    while (!(inb(com_base + UART_LSR) & LSR_THRE))
        ;
}

/* =============================================================================
 * serial_init() — UART 16550A Başlatma Prosedürü
 * =============================================================================
 *
 * Doğru init sırası (UART 16550A Datasheet + OSDev Wiki):
 *
 *  1. IER = 0x00 → Tüm UART interrupt'larını kapat
 *     (IDT kurulmadan önce interrupt gelirse triple fault olur)
 *
 *  2. LCR |= DLAB → Baud rate divisor register'larına eriş
 *     DLAB=1 iken +0 ve +1 offset'leri DLL/DLH olarak davranır
 *
 *  3. DLL = divisor & 0xFF, DLH = divisor >> 8 → Baud rate ayarla
 *     38400 baud: divisor = 3, DLL = 3, DLH = 0
 *
 *  4. LCR = 0x03 (DLAB=0 + 8N1) → Normal moda dön, veri formatını ayarla
 *     8 data bit + no parity + 1 stop bit: endüstri standardı
 *
 *  5. FCR → FIFO'yu etkinleştir ve sıfırla
 *     Başlangıçta temiz bir buffer ile başla
 */
int serial_init(void) {
    com_base             = SERIAL_COM1_BASE;  /* 0x3F8 = COM1 */
    com_ready            = 1;
    s_uart_output_enabled = 1;
    
    outb(com_base + UART_IER, 0x00);    /* Disable all interrupts */
    outb(com_base + UART_LCR, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(com_base + 0, 0x03);           /* Set divisor to 3 (lo byte) 38400 baud */
    outb(com_base + 1, 0x00);           /*                  (hi byte) */
    outb(com_base + UART_LCR, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(com_base + UART_FCR, 0xC7);    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(com_base + UART_MCR, 0x0B);    /* IRQs enabled, RTS/DSR set */
    
    return SERIAL_OK;
}

/* =============================================================================
 * serial_putchar_raw() — Ham karakter gönderimi (\n → \r\n dönüşümü yok)
 * =============================================================================
 */
void serial_putchar_raw(char c) {
    /* Write directly to COM1 port 0x3F8.
     * We use the literal address as a safety net — if com_base was
     * somehow corrupted, this still outputs to the correct UART port.
     * We skip serial_wait_tx() because on WSL+QEMU, LSR polling can loop forever. */
    outb(0x3F8 + UART_THR, (uint8_t)c);
}

/* =============================================================================
 * serial_putchar() — Karakter gönder, \n → \r\n çevir
 * =============================================================================
 *
 * Seri terminal emülatörleri (QEMU stdio, picocom, minicom) satır sonunu
 * iki karakter olarak bekler:
 *   CR (0x0D, \r) → imleci satır başına taşı
 *   LF (0x0A, \n) → bir satır aşağı in
 *
 * Sadece \n gönderilirse: imleç satır başına dönmez, bir sonraki satırın
 * başında değil orta-alt noktasından yazmaya devam eder ("staircase efekti").
 */
/* s_uart_output_enabled is declared at top of file, near other driver state vars */

void serial_set_uart_output(int enabled) {
    s_uart_output_enabled = enabled;
}

void serial_putchar(char c) {
    /* dmesg / kernel log ring buffer: her karakteri kalıcı arabelleğe de yaz.
     * Bu, UART fiziksel olarak bağlı olmasa/devre dışı bırakılsa bile
     * kernel'in tüm geçmiş çıktısının 'dmesg' ile geri getirilebilmesini sağlar. */
    klog_putchar(c);

    if (s_uart_output_enabled && com_ready) {
        if (c == '\n') {
            serial_putchar_raw('\r');   /* önce CR: imleci satır başına al */
        }
        serial_putchar_raw(c);          /* sonra LF (veya normal karakter) */
    }

    /* VBE terminal aynalama: etkinleştirilmişse her karakteri terminale de yaz */
    if (s_vbe_mirror_fn) {
        s_vbe_mirror_fn(c);
    }
    
    /* API capture aynalama: etkinleştirilmişse yakala */
    if (s_capture_mirror_fn) {
        s_capture_mirror_fn(c);
    }
}

/* =============================================================================
 * serial_puts() — Null-terminated string gönder
 * =============================================================================
 */
void serial_puts(const char *s) {
    if (!s) return;
    while (*s) {
        serial_putchar(*s++);   /* her karakter için putchar → \n dönüşümü uygulanır */
    }
}

/* =============================================================================
 * serial_has_char() — RX buffer'da karakter var mı?
 * =============================================================================
 */
int serial_has_char(void) {
    if (!com_ready) return 0;
    return (inb(com_base + UART_LSR) & LSR_DR);
}

/* =============================================================================
 * serial_getchar() — Karakter oku (polling ile bloke olur)
 * =============================================================================
 */
char serial_getchar(void) {
    if (!com_ready) return 0;
    while (!serial_has_char()) {
        /* wait */
    }
    return inb(com_base + UART_RBR);
}

/* =============================================================================
 * İÇ YARDIMCI FONKSİYONLAR — serial_printf() için
 * =============================================================================
 * Bu fonksiyonlar static'tir — sadece bu dosya içinde kullanılır.
 * stdlib olmadan: printf, sprintf, itoa — hiçbirini kullanamayız.
 * Sıfırdan yazıyoruz.
 */

/* uint32_t → unsigned decimal string → seri porta yazar */
static void print_uint(uint32_t val) {
    char  buf[12];   /* max uint32 = 4294967295 (10 digit) + null + güvenlik */
    int   i = 11;

    buf[i] = '\0';

    if (val == 0) {
        serial_putchar('0');
        return;
    }

    /* Sondan başa doğru rakamları yerleştir */
    while (val > 0) {
        buf[--i] = (char)('0' + (val % 10));  /* son rakamı al */
        val /= 10;                             /* bir basamak sola kay */
    }

    /* Oluşan string'i gönder (&buf[i] → ilk anlamlı rakam) */
    serial_puts(&buf[i]);
}

/* int32_t → signed decimal string → seri porta yazar */
static void print_int(int32_t val) {
    if (val < 0) {
        serial_putchar_raw('-');
        /*
         * INT32_MIN (-2147483648) için -val overflow oluşur (INT32_MAX = 2147483647).
         * Güvenli yaklaşım: önce +1 ekle, uint'e cast et, negasyonu al, sonra +1 ekle.
         * Bu sayede INT32_MIN bile doğru işlenir.
         */
        print_uint((uint32_t)(-(val + 1)) + 1U);
    } else {
        print_uint((uint32_t)val);
    }
}

/* uint32_t → hexadecimal string → seri porta yazar
 *
 * uppercase = 0 → a-f küçük harf (örn: deadbeef)
 * uppercase = 1 → A-F büyük harf (örn: DEADBEEF)
 *
 * Her zaman 8 digit yazar (leading zero dahil) — örn: 0x0000001F
 * Bu, kernel debug'da register değerlerini okumayı kolaylaştırır.
 */
static void print_hex(uint32_t val, int uppercase) {
    const char *lo = "0123456789abcdef";
    const char *hi = "0123456789ABCDEF";
    const char *tbl = uppercase ? hi : lo;
    char buf[9];   /* 8 hex digit + null terminator */
    int  i;

    buf[8] = '\0';
    /* MSB'den LSB'ye doğru her 4 biti bir hex digit'e çevir */
    for (i = 7; i >= 0; i--) {
        buf[i] = tbl[val & 0xFU];   /* en düşük 4 bit → tablo'dan karakter */
        val >>= 4;                   /* bir nibble sola kay */
    }

    serial_puts(buf);   /* 8-digit hex string gönder */
}

/* Compact hex print (leading zero'ları kırpar) — %x ve %X için */
static void print_hex_compact(uint32_t val, int uppercase) {
    if (val == 0) {
        serial_putchar('0');
        return;
    }

    const char *lo = "0123456789abcdef";
    const char *hi = "0123456789ABCDEF";
    const char *tbl = uppercase ? hi : lo;
    char buf[9];
    int i = 8;
    buf[8] = '\0';

    while (val > 0) {
        buf[--i] = tbl[val & 0xFU];
        val >>= 4;
    }

    serial_puts(&buf[i]);
}

/* uint32_t → binary string → seri porta yazar
 *
 * 32-bit değeri bit bit yazar, her 8 biti '_' ile ayırır.
 * Örnek: 0xFF → "0b00000000_00000000_00000000_11111111"
 *
 * KULLANIM: Flag register'larını, PIC IMR gibi bitmask değerleri görmek için.
 * CPU register'larını bit seviyesinde debug ederken çok kullanışlı.
 */
static void print_bin(uint32_t val) {
    int i;
    for (i = 31; i >= 0; i--) {
        serial_putchar_raw((char)('0' + ((val >> i) & 1U)));  /* bit'i '0'/'1' karakterine çevir */
        /* Her 8 bitten sonra '_' ekle (bit 0 hariç — son grup) */
        if (i > 0 && i % 8 == 0) {
            serial_putchar_raw('_');
        }
    }
}

/* =============================================================================
 * serial_printf() — Minimal kernel debug printf
 * =============================================================================
 *
 * Standart printf'in freestanding C için yeniden yazılmış hali.
 * <stdarg.h> kullanır — GCC freestanding modunda mevcut olan tek variadic
 * argüman mekanizması (va_list, va_start, va_end, va_arg).
 *
 * i386 cdecl calling convention:
 *   - Argümanlar stack'e sağdan sola push edilir
 *   - va_arg() bu stack frame'i gezer
 *   - va_end() temizlik yapar (i386'da genellikle no-op ama iyi pratik)
 *
 * Format string tarama: char * ile linear scan, '%' bulunca specifier analiz.
 * =============================================================================
 */
void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);   /* variadic argüman listesini fmt'den sonraki argümanla başlat */

    while (*fmt) {

        if (*fmt != '%') {
            /* Normal karakter — doğrudan gönder, döngüye devam et */
            serial_putchar(*fmt++);
            continue;
        }

        fmt++;   /* '%' karakterini atla */

        switch (*fmt) {

            case 'c':
                /*
                 * %c — karakter
                 * C standardı: char, int olarak promote edilir.
                 * va_arg'dan int olarak alınmalı, sonra char'a cast edilmeli.
                 */
                serial_putchar((char)va_arg(args, int));
                break;

            case 's': {
                /*
                 * %s — null-terminated string
                 * NULL pointer koruması: NULL gelirse "(null)" yazar.
                 */
                const char *str = va_arg(args, const char *);
                serial_puts(str ? str : "(null)");
                break;
            }

            case 'd':
                /* %d — signed 32-bit decimal integer */
                print_int((int32_t)va_arg(args, int));
                break;

            case 'u':
                /* %u — unsigned 32-bit decimal integer */
                print_uint((uint32_t)va_arg(args, unsigned int));
                break;

            case 'x':
                /* %x — compact hex, küçük harf (örn: 0x1e) */
                print_hex_compact((uint32_t)va_arg(args, unsigned int), 0);
                break;

            case 'X':
                /* %X — compact hex, büyük harf (örn: 0x1E) */
                print_hex_compact((uint32_t)va_arg(args, unsigned int), 1);
                break;

            case 'p':
                /*
                 * %p — pointer (32-bit i386 flat memory model varsayımı)
                 * void* → uint32_t (i386 32-bit pointer)
                 * "0x" prefix + 8-digit hex (x86_64 için 16-digit gerekecektir)
                 */
                serial_puts("0x");
                print_hex((uint32_t)(uintptr_t)va_arg(args, void *), 0);
                break;

            case 'b':
                /*
                 * %b — binary (non-standard, ZeruX'a özel)
                 * Hardware register flag'lerini debug ederken çok kullanışlı.
                 * Örnek: serial_printf("EFLAGS: %b\n", eflags);
                 */
                serial_puts("0b");
                print_bin((uint32_t)va_arg(args, unsigned int));
                break;

            case '%':
                /* %% — literal '%' karakteri */
                serial_putchar('%');
                break;

            default:
                /* Bilinmeyen specifier: '%' ve karakteri olduğu gibi yaz */
                serial_putchar('%');
                serial_putchar(*fmt);
                break;
        }

        fmt++;   /* specifier karakterini atla */
    }

    va_end(args);   /* variadic argüman listesini kapat */
}
