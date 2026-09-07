/* =============================================================================
 * ZeruX OS — UHCI (USB 1.1 Host Controller) Driver Implementation
 * File: kernel/drivers/uhci.c
 * =============================================================================
 * Tasarım notları için kernel/include/uhci.h başlığındaki yorumlara bakınız.
 *
 * KAPSAM (bilinçli sınırlama, v1):
 *   - Tek bir HID interrupt-IN cihazı (fare) desteklenir. Aynı anda birden
 *     fazla HID cihazı (örn. fare + klavye) desteklemek için pool'daki
 *     "kalıcı interrupt TD" tekilliğinin cihaz başına çoğaltılması gerekir
 *     — mimari buna hazırdır (bkz. UHCI_MAX_INTERRUPT_DEVICES), sadece v1
 *     kapsamı dışında bırakıldı.
 *   - Sadece Control ve Interrupt transfer tipleri implemente edildi
 *     (Bulk/Isochronous yok — USB depolama/ses için ayrı bir iş).
 * ============================================================================= */

#include "uhci.h"
#include "usb.h"
#include "pci.h"
#include "ports.h"
#include "pmm.h"
#include "serial.h"
#include <stddef.h>

/* =============================================================================
 * UHCI I/O Register Ofsetleri (BAR4'ten okunan IO Base'e göre)
 * ============================================================================= */
#define UHCI_REG_USBCMD    0x00
#define UHCI_REG_USBSTS    0x02
#define UHCI_REG_USBINTR   0x04
#define UHCI_REG_FRNUM     0x06
#define UHCI_REG_FRBASEADD 0x08
#define UHCI_REG_SOFMOD    0x0C
#define UHCI_REG_PORTSC1   0x10
#define UHCI_REG_PORTSC2   0x12

#define UHCI_CMD_RS        (1 << 0)  /* Run/Stop */
#define UHCI_CMD_HCRESET   (1 << 1)
#define UHCI_CMD_GRESET    (1 << 2)
#define UHCI_CMD_CF        (1 << 6)  /* Configure Flag */
#define UHCI_CMD_MAXP64    (1 << 7)  /* Max Packet = 64 byte */

#define UHCI_STS_HALTED    (1 << 5)

#define UHCI_PORTSC_CONNECT_STATUS  (1 << 0)
#define UHCI_PORTSC_CONNECT_CHANGE  (1 << 1)
#define UHCI_PORTSC_ENABLE          (1 << 2)
#define UHCI_PORTSC_ENABLE_CHANGE   (1 << 3)
#define UHCI_PORTSC_LOW_SPEED       (1 << 8)
#define UHCI_PORTSC_RESET           (1 << 9)
#define UHCI_PORTSC_WRITE_CLEAR     (UHCI_PORTSC_CONNECT_CHANGE | UHCI_PORTSC_ENABLE_CHANGE)

/* Link Pointer bit alanları (Frame List girdileri, QH.link/element, TD.link ortak) */
#define UHCI_LP_TERMINATE   (1u << 0)
#define UHCI_LP_QH          (1u << 1)
#define UHCI_LP_DEPTH_FIRST (1u << 2)
#define UHCI_LP_ADDR_MASK   (0xFFFFFFF0u)

/* TD Control/Status DWORD bit alanları */
#define TD_CS_ACTLEN_MASK   0x7FFu
#define TD_CS_BITSTUFF      (1u << 17)
#define TD_CS_CRC_TIMEOUT   (1u << 18)
#define TD_CS_NAK           (1u << 19)
#define TD_CS_BABBLE        (1u << 20)
#define TD_CS_DATABUF_ERR   (1u << 21)
#define TD_CS_STALLED       (1u << 22)
#define TD_CS_ACTIVE        (1u << 23)
#define TD_CS_IOC           (1u << 24)
#define TD_CS_ISO           (1u << 25)
#define TD_CS_LOWSPEED      (1u << 26)
#define TD_CS_CERR_SHIFT    27
#define TD_CS_SPD           (1u << 29)
#define TD_CS_ANY_ERROR     (TD_CS_BITSTUFF | TD_CS_CRC_TIMEOUT | TD_CS_NAK | \
                              TD_CS_BABBLE | TD_CS_DATABUF_ERR | TD_CS_STALLED)

/* TD Token DWORD bit alanları */
#define TD_TOK_PID_SETUP    0x2D
#define TD_TOK_PID_IN       0x69
#define TD_TOK_PID_OUT      0xE1
#define TD_TOK_ENDPOINT_SHIFT 15
#define TD_TOK_ADDR_SHIFT     8
#define TD_TOK_TOGGLE_SHIFT   19
#define TD_TOK_MAXLEN_SHIFT   21

static inline uint32_t td_token(uint8_t pid, uint8_t addr, uint8_t endpoint,
                                 uint8_t toggle, uint16_t len) {
    uint32_t maxlen_field = (len == 0) ? 0x7FFu : ((uint32_t)(len - 1) & 0x7FFu);
    return (uint32_t)pid
         | ((uint32_t)addr << TD_TOK_ADDR_SHIFT)
         | ((uint32_t)endpoint << TD_TOK_ENDPOINT_SHIFT)
         | ((uint32_t)(toggle & 1) << TD_TOK_TOGGLE_SHIFT)
         | (maxlen_field << TD_TOK_MAXLEN_SHIFT);
}

/* =============================================================================
 * Queue Head / Transfer Descriptor yapıları
 * Her ikisi de HC tarafından okunacağı için 16-byte hizalı olmak ZORUNDA
 * (Link Pointer'ların alt 4 biti bayrak olarak kullanılıyor).
 * ============================================================================= */
typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t link;
    volatile uint32_t element;
    uint32_t reserved[2]; /* 16 bayta tamamla */
} uhci_qh_t;

typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t link;
    volatile uint32_t cs;
    volatile uint32_t token;
    volatile uint32_t buffer;
    /* ---- yazılım alanları (HC bunları görmez) ---- */
    volatile uint32_t sw_pending;
    void (*sw_callback)(usb_device_t *dev, const uint8_t *data, uint32_t len);
    usb_device_t *sw_dev;
    uint32_t sw_reserved;
} uhci_td_t;

/* =============================================================================
 * Sürücü Durumu
 * ============================================================================= */
static uint16_t     g_io_base   = 0;
static bool         g_present   = false;
static uint32_t    *g_frame_list = NULL; /* 1024 x 32-bit, pmm_alloc_block() ile ayrılır */

/* Kalıcı schedule düğümleri + tek bir HID cihazı için kalıcı interrupt TD.
 * Kontrol transferleri için sabit sayıda scratch TD (bump allocator, her
 * control_transfer çağrısının başında sıfırlanır — transferler sıralı
 * (blocking) yürütüldüğü için önceki TD'lerin donanım tarafından hâlâ
 * kullanılıyor olma ihtimali yoktur). */
#define UHCI_SCRATCH_TD_COUNT 24

static uhci_qh_t g_int_qh     __attribute__((aligned(16)));
static uhci_qh_t g_control_qh __attribute__((aligned(16)));
static uhci_td_t g_mouse_td   __attribute__((aligned(16)));
static uhci_td_t g_scratch_td[UHCI_SCRATCH_TD_COUNT] __attribute__((aligned(16)));

static inline uint32_t phys_of(const void *p) {
    /* ZeruX'te kernel belleği identity-mapped (virtual == physical) olduğu
     * varsayımı, rtl8168.c ve diğer DMA kullanan sürücülerle aynı kuralı
     * izler. */
    return (uint32_t)(uintptr_t)p;
}

static void uhci_reg_write16(uint16_t off, uint16_t val) { outw((uint16_t)(g_io_base + off), val); }
static uint16_t uhci_reg_read16(uint16_t off)             { return inw((uint16_t)(g_io_base + off)); }
static void uhci_reg_write32(uint16_t off, uint32_t val)  { outl((uint16_t)(g_io_base + off), val); }

/* =============================================================================
 * Control Transfer
 * ============================================================================= */
static int uhci_control_transfer(usb_device_t *dev, const usb_setup_packet_t *setup, void *data) {
    bool data_in = (setup->bmRequestType & USB_RT_DIR_IN) != 0;
    uint16_t remaining = setup->wLength;
    uint8_t *bufp = (uint8_t*)data;

    uint32_t idx = 0; /* g_scratch_td bump index */
    uint8_t toggle = 0;

    /* --- SETUP evresi (her zaman DATA0, PID=SETUP) --- */
    uhci_td_t *setup_td = &g_scratch_td[idx++];
    setup_td->token  = td_token(TD_TOK_PID_SETUP, dev->address, 0, 0, 8);
    setup_td->buffer = phys_of(setup);
    setup_td->cs     = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT) |
                        (dev->low_speed ? TD_CS_LOWSPEED : 0);
    toggle = 1;

    uhci_td_t *prev_td = setup_td;
    uhci_td_t *first_td = setup_td;

    /* --- DATA evresi (varsa) --- */
    while (remaining > 0 && idx < UHCI_SCRATCH_TD_COUNT - 1) {
        uint16_t chunk = remaining;
        if (chunk > dev->max_packet_size0) chunk = dev->max_packet_size0;

        uhci_td_t *td = &g_scratch_td[idx++];
        td->token  = td_token(data_in ? TD_TOK_PID_IN : TD_TOK_PID_OUT,
                               dev->address, 0, toggle, chunk);
        td->buffer = phys_of(bufp);
        td->cs     = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT) |
                     (dev->low_speed ? TD_CS_LOWSPEED : 0);

        prev_td->link = phys_of(td) | UHCI_LP_DEPTH_FIRST; /* TD-> TD, depth-first */
        prev_td = td;

        bufp      += chunk;
        remaining -= chunk;
        toggle    ^= 1;
    }

    /* --- STATUS evresi (her zaman DATA1, zero-length, ters yön) --- */
    bool had_data = (setup->wLength > 0);
    uint8_t status_pid = (had_data && data_in) ? TD_TOK_PID_OUT : TD_TOK_PID_IN;
    uhci_td_t *status_td = &g_scratch_td[idx++];
    status_td->token  = td_token(status_pid, dev->address, 0, 1, 0);
    status_td->buffer = 0;
    status_td->cs     = TD_CS_ACTIVE | TD_CS_IOC | (3u << TD_CS_CERR_SHIFT) | TD_CS_SPD |
                         (dev->low_speed ? TD_CS_LOWSPEED : 0);
    status_td->link   = UHCI_LP_TERMINATE;

    prev_td->link = phys_of(status_td) | UHCI_LP_DEPTH_FIRST;

    /* --- Zinciri kontrol QH'sine tak ve çalıştır --- */
    g_control_qh.element = phys_of(first_td);

    /* --- Tamamlanmasını bekle (blocking; sadece enumeration sırasında
     *     kullanıldığından kabul edilebilir). Zaman aşımı: yaklaşık birkaç
     *     yüz milisaniyeye tekabül eden bir döngü sayacı. --- */
    uint32_t timeout = 20000000;
    while (timeout--) {
        uint32_t elem = g_control_qh.element;
        if (elem & UHCI_LP_TERMINATE) {
            /* Tüm zincir başarıyla tamamlandı */
            uint32_t transferred = setup->wLength - remaining;
            /* Son data TD'sinin gerçek uzunluğuna göre kısmi paketleri de
             * hesaba katmak istersek burada actlen okunabilir; basit
             * kullanım için istenen toplam uzunluğu döndürmek yeterli. */
            (void)transferred;
            return setup->wLength;
        }
        uhci_td_t *cur = (uhci_td_t*)(uintptr_t)(elem & UHCI_LP_ADDR_MASK);
        uint32_t cs = cur->cs;
        if (!(cs & TD_CS_ACTIVE)) {
            if (cs & TD_CS_ANY_ERROR) {
                serial_printf("[UHCI] Control transfer failed, cs=0x%x\n", cs);
                return -1;
            }
            /* Active=0 ama Terminate de değil ve hata da yok: HC henüz
             * element'i ilerletmedi, bir sonraki döngüde tekrar bakılır. */
        }
    }

    serial_printf("[UHCI] Control transfer TIMEOUT.\n");
    return -1;
}

/* =============================================================================
 * Interrupt-IN kurulumu (tek cihaz, bkz. dosya başı kapsam notu)
 * ============================================================================= */
static int uhci_setup_interrupt_in(usb_device_t *dev, uint8_t endpoint,
                                    uint16_t max_packet_size, uint8_t interval,
                                    void (*callback)(usb_device_t*, const uint8_t*, uint32_t)) {
    (void)interval; /* v1: her frame'de poll ediyoruz (1ms), cihazın istediği
                        aralık zaten en az bu kadar sık kontrol edildiği için
                        sorun teşkil etmez. */
    if (max_packet_size > 8) max_packet_size = 8; /* boot-protocol mouse rapor payload'ı için yeterli */

    static uint8_t s_mouse_report_buf[8] __attribute__((aligned(4)));

    g_mouse_td.sw_dev      = dev;
    g_mouse_td.sw_callback = callback;
    g_mouse_td.sw_pending  = 1;
    g_mouse_td.buffer      = phys_of(s_mouse_report_buf);
    g_mouse_td.token       = td_token(TD_TOK_PID_IN, dev->address,
                                       endpoint & 0x0F, 0, max_packet_size);
    g_mouse_td.link        = UHCI_LP_TERMINATE;
    g_mouse_td.cs          = TD_CS_ACTIVE | TD_CS_IOC | (3u << TD_CS_CERR_SHIFT) |
                              TD_CS_SPD |
                              (dev->low_speed ? TD_CS_LOWSPEED : 0);

    g_int_qh.element = phys_of(&g_mouse_td);
    serial_printf("[UHCI] Interrupt-IN endpoint 0x%x armed for polling.\n", endpoint);
    return 0;
}

static const usb_hcd_ops_t g_uhci_ops = {
    .control_transfer   = uhci_control_transfer,
    .setup_interrupt_in  = uhci_setup_interrupt_in,
};

/* =============================================================================
 * uhci_poll() — timer_callback()'ten (~10ms'de bir) çağrılır
 * ============================================================================= */
void uhci_poll(void) {
    if (!g_present) return;

    /* Donanım çökmüş veya durmuş mu kontrol et ve status bitlerini temizle */
    uint16_t sts = uhci_reg_read16(UHCI_REG_USBSTS);
    if (sts) {
        uhci_reg_write16(UHCI_REG_USBSTS, sts); /* W1C: clear bits */
    }
    if (sts & UHCI_STS_HALTED) {
        serial_printf("[UHCI] FATAL: Host Controller Halted! STS=0x%x\n", sts);
        /* Hataları temizleyip tekrar başlatmayı dene */
        uhci_reg_write16(UHCI_REG_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP64);
        return;
    }

    if (!g_mouse_td.sw_pending) return;

    uint32_t cs = g_mouse_td.cs;
    if (cs & TD_CS_ACTIVE) return; /* henüz tamamlanmadı, sıradaki tick'te tekrar bak */

    uint8_t toggle_next = (uint8_t)((g_mouse_td.token >> TD_TOK_TOGGLE_SHIFT) & 1);
    uint8_t addr    = (uint8_t)((g_mouse_td.token >> TD_TOK_ADDR_SHIFT) & 0x7F);
    uint8_t ep      = (uint8_t)((g_mouse_td.token >> TD_TOK_ENDPOINT_SHIFT) & 0x0F);
    uint16_t maxlen = (uint16_t)(((g_mouse_td.token >> TD_TOK_MAXLEN_SHIFT) & 0x7FF) + 1);

    if (!(cs & TD_CS_ANY_ERROR)) {
        toggle_next ^= 1; /* Sadece başarılı veri alışverişinde toggle yap */
        uint32_t actlen = (cs & TD_CS_ACTLEN_MASK);
        actlen = (actlen == 0x7FF) ? 0 : (actlen + 1);
        if (g_mouse_td.sw_callback) {
            g_mouse_td.sw_callback(g_mouse_td.sw_dev,
                                    (const uint8_t*)(uintptr_t)g_mouse_td.buffer,
                                    actlen);
        }
    } else {
        /* Hata durumunda log yazalım ki sorunu görelim (QEMU vs) */
        serial_printf("[UHCI POLL] Error in mouse TD! cs=0x%x\n", cs);
    }

    g_mouse_td.token = td_token(TD_TOK_PID_IN, addr, ep, toggle_next, maxlen);
    g_mouse_td.cs    = TD_CS_ACTIVE | TD_CS_IOC | (3u << TD_CS_CERR_SHIFT) |
                       (g_mouse_td.cs & TD_CS_LOWSPEED);
    g_int_qh.element = phys_of(&g_mouse_td);
}

/* =============================================================================
 * Port reset + enumeration tetikleme
 * ============================================================================= */
static void uhci_reset_and_probe_port(uint16_t port_reg) {
    uint16_t sts = uhci_reg_read16(port_reg);
    if (!(sts & UHCI_PORTSC_CONNECT_STATUS)) return; /* portta cihaz yok */

    serial_printf("[UHCI] Device detected on port (reg=0x%x), resetting...\n", port_reg);

    /* Reset: en az 50ms (USB2.0 Spec 7.1.7.5). Gerçek donanımda güvenli
     * olmak için burada bolca bekliyoruz (rtl8168.c'de PS/2 reset için
     * uygulanan "gerçek donanım QEMU'dan yavaştır" prensibiyle aynı). */
    uhci_reg_write16(port_reg, UHCI_PORTSC_RESET);
    for (volatile uint32_t i = 0; i < 3000000; i++) { }
    uhci_reg_write16(port_reg, 0);
    for (volatile uint32_t i = 0; i < 100000; i++) { }

    sts = uhci_reg_read16(port_reg);
    if (!(sts & UHCI_PORTSC_CONNECT_STATUS)) {
        serial_printf("[UHCI] Device disappeared after reset.\n");
        return;
    }

    bool low_speed = (sts & UHCI_PORTSC_LOW_SPEED) != 0;

    /* Bazı UHCI denetleyicileri portu reset sonrası otomatik enable etmez;
     * SW'nin açıkça enable etmesi gerekir. */
    uhci_reg_write16(port_reg, UHCI_PORTSC_ENABLE);
    for (volatile uint32_t i = 0; i < 10000; i++) { }

    /* Change bitlerini temizle (write-1-to-clear) */
    sts = uhci_reg_read16(port_reg);
    uhci_reg_write16(port_reg, sts | UHCI_PORTSC_WRITE_CLEAR);

    serial_printf("[UHCI] Port enabled, low_speed=%d. Starting enumeration...\n", low_speed);
    usb_enumerate_device(&g_uhci_ops, (void*)(uintptr_t)port_reg, low_speed);
}

/* =============================================================================
 * uhci_init()
 * ============================================================================= */
void uhci_init(void) {
    pci_device_t *pdev = pci_find_device_by_class(0x0C, 0x03, 0x00);
    if (!pdev) {
        serial_printf("[UHCI] No UHCI controller found on PCI bus.\n");
        return;
    }

    g_io_base = (uint16_t)(pdev->bar4 & 0xFFFFFFFCu);
    serial_printf("[UHCI] Controller found at bus=%d dev=%d func=%d io_base=0x%x\n",
                  pdev->bus, pdev->device, pdev->function, g_io_base);

    /* PCI bus mastering'i etkinleştir (Command register, bit2) — UHCI'nin
     * frame list'e ve TD buffer'larına erişebilmesi için gereklidir. */
    uint32_t pci_cmd = pci_config_read32(pdev->bus, pdev->device, pdev->function, 0x04);
    pci_config_write32(pdev->bus, pdev->device, pdev->function, 0x04, pci_cmd | 0x0004);

    /* --- Global + Host Controller reset --- */
    uhci_reg_write16(UHCI_REG_USBCMD, UHCI_CMD_GRESET);
    for (volatile uint32_t i = 0; i < 1000000; i++) { }
    uhci_reg_write16(UHCI_REG_USBCMD, 0);

    uhci_reg_write16(UHCI_REG_USBCMD, UHCI_CMD_HCRESET);
    uint32_t hc_reset_wait = 1000000;
    while ((uhci_reg_read16(UHCI_REG_USBCMD) & UHCI_CMD_HCRESET) && hc_reset_wait--) { }

    /* --- Frame list (1024 x 32-bit, 4KB, sayfa hizalı) --- */
    g_frame_list = (uint32_t*)pmm_alloc_block();
    if (!g_frame_list) {
        serial_printf("[UHCI] Frame list allocation failed.\n");
        return;
    }

    /* --- Schedule: her frame -> INT_QH -> CONTROL_QH -> Terminate --- */
    g_int_qh.link       = phys_of(&g_control_qh) | UHCI_LP_QH;
    g_int_qh.element    = UHCI_LP_TERMINATE;
    g_control_qh.link    = UHCI_LP_TERMINATE;
    g_control_qh.element = UHCI_LP_TERMINATE;

    uint32_t frame_entry = phys_of(&g_int_qh) | UHCI_LP_QH;
    for (int i = 0; i < 1024; i++) g_frame_list[i] = frame_entry;

    uhci_reg_write16(UHCI_REG_FRNUM, 0);
    uhci_reg_write32(UHCI_REG_FRBASEADD, phys_of(g_frame_list));
    uhci_reg_write16(UHCI_REG_SOFMOD, 0x40);
    uhci_reg_write16(UHCI_REG_USBINTR, 0); /* polling modeli: HC kesmeleri kapalı */

    /* --- Controller'ı çalıştır --- */
    uhci_reg_write16(UHCI_REG_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP64);

    uint32_t run_wait = 100000;
    while ((uhci_reg_read16(UHCI_REG_USBSTS) & UHCI_STS_HALTED) && run_wait--) { }

    g_present = true;
    serial_printf("[UHCI] Controller running. Probing root hub ports...\n");

    uhci_reset_and_probe_port(UHCI_REG_PORTSC1);
    uhci_reset_and_probe_port(UHCI_REG_PORTSC2);
}
