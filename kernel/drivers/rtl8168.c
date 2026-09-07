/* =============================================================================
 * ZeruX OS — RTL8168/8111 (incl. RTL8111DL) Gigabit NIC Driver
 * File: kernel/drivers/rtl8168.c
 * =============================================================================
 * Register offsets, bit definitions and the two-write ("High before Low")
 * descriptor-address quirk in this file are taken from the real Linux
 * r8169 in-tree driver's register map, adapted to ZeruX's MMIO-plus-paging
 * infrastructure. This is a from-scratch, simplified re-implementation:
 * no checksum offload, no VLAN, no jumbo frames, single interrupt mode —
 * just reliable send/receive so the existing net.c stack can talk to a
 * Gigabit-class card instead of only the 10/100 RTL8139.
 *
 * IMPORTANT DIFFERENCE vs rtl8139.c:
 *   RTL8139 is accessed purely via legacy port I/O (in/out).
 *   RTL8168/8111 is a "C+ mode" descriptor-ring MAC: the driver builds
 *   RX/TX descriptor rings in RAM and the NIC DMAs to/from them. Control
 *   registers are accessed via MMIO (a PCI memory BAR mapped into our
 *   page tables), not port I/O. That mapping step is why this driver
 *   needs vmm.h, which rtl8139.c does not.
 * =============================================================================
 */

#include "rtl8168.h"
#include "pci.h"
#include "ports.h"
#include "serial.h"
#include "pmm.h"
#include "vmm.h"
#include "irq.h"
#include <stddef.h>

/* =============================================================================
 * Register offsets (byte offsets into the MMIO BAR)
 * =============================================================================
 */
#define R_MAC0                  0x00  /* Station MAC address, 6 bytes    */
#define R_MAR0                  0x08  /* Multicast filter, 8 bytes       */
#define R_TX_DESC_ADDR_LOW      0x20
#define R_TX_DESC_ADDR_HIGH     0x24
#define R_CHIP_CMD               0x37
#define R_TX_POLL                0x38
#define R_INTR_MASK               0x3C
#define R_INTR_STATUS             0x3E
#define R_TX_CONFIG                0x40
#define R_RX_CONFIG                0x44
#define R_CFG_9346                    0x50
#define R_CONFIG1                     0x52
#define R_PHY_ACCESS                    0x60
#define R_PHY_STATUS                    0x6C
#define R_RX_MAX_SIZE                       0xDA
#define R_CPLUS_CMD                         0xE0
#define R_RX_DESC_ADDR_LOW                  0xE4
#define R_RX_DESC_ADDR_HIGH                 0xE8
#define R_MAX_TX_PACKET_SIZE                0xEC

/* ChipCmd (0x37) bits */
#define CMD_RESET   0x10
#define CMD_RX_ENB  0x08
#define CMD_TX_ENB  0x04

/* IntrStatus / IntrMask (0x3E / 0x3C) bits */
#define INT_SYS_ERR   0x8000
#define INT_LINK_CHG  0x0020
#define INT_TX_ERR    0x0008
#define INT_TX_OK     0x0004
#define INT_RX_ERR    0x0002
#define INT_RX_OK     0x0001

/* RxConfig (0x44) bits */
#define RXCFG_128_INT_EN  (1 << 15)
#define RXCFG_MULTI_EN    (1 << 14)
#define RXCFG_DMA_BURST_UNLIMITED (7 << 8)
#define RXCFG_ACCEPT_ERR    0x20
#define RXCFG_ACCEPT_RUNT   0x10
#define RXCFG_ACCEPT_BCAST  0x08
#define RXCFG_ACCEPT_MCAST  0x04
#define RXCFG_ACCEPT_PHYS   0x02   /* accept packets matching our MAC */
#define RXCFG_ACCEPT_ALLPHYS 0x01  /* promiscuous */

/* TxConfig (0x40) bits */
#define TXCFG_DMA_BURST_UNLIMITED (7 << 8)   /* TxDMAShift = 8 */
#define TXCFG_IFG_STD             (3 << 24)  /* InterFrameGap = shortest legal gap */

/* Cfg9346 (0x50) bits */
#define CFG9346_LOCK   0x00
#define CFG9346_UNLOCK 0xC0

/* CPlusCmd (0xE0) bits */
#define CPCMD_MULRW   (1 << 3)   /* PCIMulRW: allow burst PCI DMA read/write */

/* TxPoll (0x38) bits */
#define TXPOLL_NPQ  0x40  /* kick the normal-priority TX queue */

/* PHYstatus (0x6C) bits */
#define PHYSTAT_1000F      0x10
#define PHYSTAT_100        0x08
#define PHYSTAT_10         0x04
#define PHYSTAT_LINK       0x02
#define PHYSTAT_FULL_DUP   0x01

/* Descriptor bits (opts1, first dword of both RX and TX descriptors) */
#define DESC_OWN    (1u << 31)  /* 1 = owned by NIC */
#define DESC_EOR    (1u << 30)  /* End Of Ring       */
#define DESC_FS     (1u << 29)  /* First Segment     */
#define DESC_LS     (1u << 28)  /* Last Segment      */
#define DESC_LEN_MASK 0x3FFF

/* =============================================================================
 * Descriptor & ring layout
 * =============================================================================
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t addr_lo;
    uint32_t addr_hi;
} rtl_desc_t;
#pragma pack(pop)

#define NUM_RX_DESC   32
#define NUM_TX_DESC   16
#define RTL_BUF_SIZE  1536   /* > 1518 (max std Ethernet frame incl. VLAN/CRC headroom) */

static volatile uint8_t *rtl_mmio  = NULL;
static uint8_t  rtl_irq            = 0;
static uint8_t  mac_address[6];
static bool     rtl_present        = false;

static rtl_desc_t *rx_ring = NULL;
static rtl_desc_t *tx_ring = NULL;
static uint8_t     *rx_buf[NUM_RX_DESC];
static uint8_t     *tx_buf[NUM_TX_DESC];
static uint32_t     cur_rx = 0;
static uint32_t     cur_tx = 0;

/* =============================================================================
 * MMIO accessors
 * =============================================================================
 */
static inline uint8_t  RTL_R8(uint32_t reg)  { return *(volatile uint8_t  *)(rtl_mmio + reg); }
static inline uint16_t RTL_R16(uint32_t reg) { return *(volatile uint16_t *)(rtl_mmio + reg); }
static inline uint32_t RTL_R32(uint32_t reg) { return *(volatile uint32_t *)(rtl_mmio + reg); }
static inline void RTL_W8(uint32_t reg, uint8_t val)   { *(volatile uint8_t  *)(rtl_mmio + reg) = val; }
static inline void RTL_W16(uint32_t reg, uint16_t val) { *(volatile uint16_t *)(rtl_mmio + reg) = val; }
static inline void RTL_W32(uint32_t reg, uint32_t val) { *(volatile uint32_t *)(rtl_mmio + reg) = val; }

/* =============================================================================
 * map_mmio_bar() — find the card's memory-mapped register BAR and identity-map
 * it into the kernel's page directory (it is device memory, not RAM, so it is
 * never covered by vmm_init()'s RAM identity map).
 * =============================================================================
 */
static volatile uint8_t *map_mmio_bar(pci_device_t *pdev) {
    uint32_t bars[5] = { pdev->bar1, pdev->bar2, pdev->bar3, pdev->bar4, pdev->bar5 };

    for (int i = 0; i < 5; i++) {
        uint32_t bar = bars[i];
        if (bar == 0) continue;
        if (bar & 0x1) continue; /* bit0 == 1 -> I/O space BAR, skip it */

        uint32_t phys = bar & ~0xFu; /* mask off type/prefetch flag bits */
        if (phys == 0) continue;

        /* Map 2 pages (8 KB) — comfortably covers every offset we use (<= 0xEC) */
        page_directory_t *pdir = vmm_get_kernel_page_directory();
        for (uint32_t off = 0; off < 2 * VMM_PAGE_SIZE; off += VMM_PAGE_SIZE) {
            vmm_map_page(pdir, phys + off, phys + off,
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_CACHE_DISABLE);
            vmm_flush_tlb_entry(phys + off);
        }

        serial_printf("[RTL8168] MMIO BAR%d mapped at phys 0x%x\n", i + 1, phys);
        return (volatile uint8_t *)phys;
    }
    return NULL;
}

/* =============================================================================
 * rtl8168_handler() — IRQ bottom half: ack status, drain RX ring, log link chg
 * =============================================================================
 */
static void rtl8168_handler(registers_t *regs) {
    (void)regs;
    if (!rtl_mmio) return;

    uint16_t status = RTL_R16(R_INTR_STATUS);
    if (!status || status == 0xFFFF) return;

    /* Acknowledge whatever fired */
    RTL_W16(R_INTR_STATUS, status);

    if (status & INT_LINK_CHG) {
        uint8_t phy = RTL_R8(R_PHY_STATUS);
        if (phy & PHYSTAT_LINK) {
            const char *speed = (phy & PHYSTAT_1000F) ? "1000" :
                                 (phy & PHYSTAT_100)   ? "100"  :
                                 (phy & PHYSTAT_10)    ? "10"   : "?";
            serial_printf("[RTL8168] Link UP: %sMbps %s-duplex\n",
                          speed, (phy & PHYSTAT_FULL_DUP) ? "full" : "half");
        } else {
            serial_printf("[RTL8168] Link DOWN.\n");
        }
    }

    if (status & (INT_RX_OK | INT_RX_ERR)) {
        /* Walk the RX ring while the NIC has handed descriptors back to us */
        while (!(rx_ring[cur_rx].opts1 & DESC_OWN)) {
            uint32_t opts1 = rx_ring[cur_rx].opts1;

            if ((opts1 & (DESC_FS | DESC_LS)) == (DESC_FS | DESC_LS)) {
                uint32_t len = opts1 & DESC_LEN_MASK;
                if (len > 4) { /* drop trailing 4-byte CRC */
                    extern void net_receive_packet(const uint8_t *packet, uint32_t len);
                    net_receive_packet(rx_buf[cur_rx], len - 4);
                }
            }

            /* Hand the descriptor back to the NIC for reuse */
            uint32_t eor = (cur_rx == NUM_RX_DESC - 1) ? DESC_EOR : 0;
            rx_ring[cur_rx].opts2 = 0;
            rx_ring[cur_rx].opts1 = DESC_OWN | eor | RTL_BUF_SIZE;

            cur_rx = (cur_rx + 1) % NUM_RX_DESC;
        }
    }

    if (status & INT_SYS_ERR) {
        serial_printf("[RTL8168] WARNING: PCI system error (SYSErr) reported.\n");
    }
}

/* =============================================================================
 * rtl8168_init()
 * =============================================================================
 */
bool rtl8168_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — RTL8168/8111 Gigabit Network Driver\n");
    serial_printf("===========================================\n");

    /* RTL8111/8168/8411/RTL8111DL all enumerate under PCI Device ID 0x8168 */
    pci_device_t *pdev = pci_find_device(0x10EC, 0x8168);
    if (!pdev) {
        serial_printf("[RTL8168] No RTL8168/8111-family device found on PCI bus.\n");
        return false;
    }

    rtl_irq = pdev->interrupt_line;
    serial_printf("[RTL8168] Found device %02x:%02x.%x, IRQ %u\n",
                  pdev->bus, pdev->device, pdev->function, rtl_irq);

    /* Enable PCI Memory Space + Bus Mastering (this card is MMIO, not I/O) */
    uint32_t pci_cmd = pci_config_read32(pdev->bus, pdev->device, pdev->function, 0x04);
    pci_cmd |= (1 << 1); /* Memory Space Enable */
    pci_cmd |= (1 << 2); /* Bus Master Enable   */
    pci_config_write32(pdev->bus, pdev->device, pdev->function, 0x04, pci_cmd);

    /* Locate & map the MMIO register BAR */
    rtl_mmio = map_mmio_bar(pdev);
    if (!rtl_mmio) {
        serial_printf("[RTL8168] ERROR: No memory BAR found — cannot continue.\n");
        return false;
    }

    /* Software reset, then wait for the bit to self-clear */
    RTL_W8(R_CHIP_CMD, CMD_RESET);
    int timeout = 100000;
    while (RTL_R8(R_CHIP_CMD) & CMD_RESET) {
        if (--timeout == 0) {
            serial_printf("[RTL8168] Reset timeout!\n");
            return false;
        }
    }
    serial_printf("[RTL8168] Software reset completed.\n");

    /* Read the station MAC address (IDR0..IDR5) */
    serial_printf("[RTL8168] MAC Address: ");
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        mac_address[i] = RTL_R8(R_MAC0 + i);
        uint8_t b = mac_address[i];
        serial_printf("%c%c%s", hex[b >> 4], hex[b & 0xF], i == 5 ? "\n" : ":");
    }

    /* --- Allocate descriptor rings (must be at least 256-byte aligned;
     *     a full physical page trivially satisfies that) --- */
    rx_ring = (rtl_desc_t *)pmm_alloc_block();
    tx_ring = (rtl_desc_t *)pmm_alloc_block();
    if (!rx_ring || !tx_ring) {
        serial_printf("[RTL8168] CRITICAL ERROR: Could not allocate descriptor rings!\n");
        return false;
    }
    for (uint32_t i = 0; i < 4096 / sizeof(rtl_desc_t); i++) {
        rx_ring[i].opts1 = 0;
        rx_ring[i].opts2 = 0;
        tx_ring[i].opts1 = 0;
        tx_ring[i].opts2 = 0;
    }

    /* --- Allocate RX packet buffers: NUM_RX_DESC * RTL_BUF_SIZE, page-rounded --- */
    uint32_t rx_pages = ((NUM_RX_DESC * RTL_BUF_SIZE) + 4095) / 4096;
    uint8_t *rx_mem = (uint8_t *)pmm_alloc_blocks(rx_pages);
    uint32_t tx_pages = ((NUM_TX_DESC * RTL_BUF_SIZE) + 4095) / 4096;
    uint8_t *tx_mem = (uint8_t *)pmm_alloc_blocks(tx_pages);
    if (!rx_mem || !tx_mem) {
        serial_printf("[RTL8168] CRITICAL ERROR: Could not allocate packet buffers!\n");
        return false;
    }

    page_directory_t *pdir = vmm_get_kernel_page_directory();
    
    for (uint32_t i = 0; i < NUM_RX_DESC; i++) {
        rx_buf[i] = rx_mem + (i * RTL_BUF_SIZE);
        uint32_t eor = (i == NUM_RX_DESC - 1) ? DESC_EOR : 0;
        rx_ring[i].addr_lo = vmm_get_physical_address(pdir, (uint32_t)rx_buf[i]);
        rx_ring[i].addr_hi = 0;
        rx_ring[i].opts2   = 0;
        rx_ring[i].opts1   = DESC_OWN | eor | RTL_BUF_SIZE;
    }

    for (uint32_t i = 0; i < NUM_TX_DESC; i++) {
        tx_buf[i] = tx_mem + (i * RTL_BUF_SIZE);
        uint32_t eor = (i == NUM_TX_DESC - 1) ? DESC_EOR : 0;
        tx_ring[i].addr_lo = vmm_get_physical_address(pdir, (uint32_t)tx_buf[i]);
        tx_ring[i].addr_hi = 0;
        tx_ring[i].opts2   = 0;
        tx_ring[i].opts1   = eor; /* NIC does not own it yet; driver fills on send */
    }
    cur_rx = 0;
    cur_tx = 0;

    /* --- Program the card --- */
    RTL_W8(R_CFG_9346, CFG9346_UNLOCK);

    /* Hardware quirk (inherited from real silicon): write *High* before *Low*
     * for both descriptor-ring base address registers. */
    RTL_W32(R_TX_DESC_ADDR_HIGH, 0);
    RTL_W32(R_TX_DESC_ADDR_LOW, vmm_get_physical_address(pdir, (uint32_t)tx_ring));
    RTL_W32(R_RX_DESC_ADDR_HIGH, 0);
    RTL_W32(R_RX_DESC_ADDR_LOW, vmm_get_physical_address(pdir, (uint32_t)rx_ring));

    RTL_W8(R_MAX_TX_PACKET_SIZE, 0x3F);           /* 0x3F * 128 = 8064 bytes, plenty */
    RTL_W16(R_RX_MAX_SIZE, RTL_BUF_SIZE + 1);

    RTL_W32(R_TX_CONFIG, TXCFG_DMA_BURST_UNLIMITED | TXCFG_IFG_STD);
    RTL_W32(R_RX_CONFIG, RXCFG_128_INT_EN | RXCFG_MULTI_EN | RXCFG_DMA_BURST_UNLIMITED |
                          RXCFG_ACCEPT_BCAST | RXCFG_ACCEPT_MCAST | RXCFG_ACCEPT_PHYS);

    /* CPCMD_MULRW (bit 3) | Rx/Tx C+ mode enable (bit 5 & bit 1) */
    RTL_W16(R_CPLUS_CMD, 0x0020 | 0x0008 | 0x0001);

    RTL_W8(R_CFG_9346, CFG9346_LOCK);

    /* Clear and then unmask the interrupts we care about */
    RTL_W16(R_INTR_STATUS, 0xFFFF);
    RTL_W16(R_INTR_MASK, INT_RX_OK | INT_RX_ERR | INT_TX_OK | INT_TX_ERR |
                          INT_LINK_CHG | INT_SYS_ERR);

    /* Hook the IRQ line */
    irq_install_handler(rtl_irq, rtl8168_handler);
    serial_printf("[RTL8168] Hooked IRQ %u.\n", rtl_irq);

    /* Finally enable RX and TX */
    RTL_W8(R_CHIP_CMD, CMD_RX_ENB | CMD_TX_ENB);

    rtl_present = true;
    serial_printf("[RTL8168] RX/TX Enabled. Initialization complete.\n");

    /* Report current link state right away (don't wait for the first
     * link-change interrupt — on some setups it may already be up). */
    uint8_t phy = RTL_R8(R_PHY_STATUS);
    if (phy & PHYSTAT_LINK) {
        const char *speed = (phy & PHYSTAT_1000F) ? "1000" :
                             (phy & PHYSTAT_100)   ? "100"  :
                             (phy & PHYSTAT_10)    ? "10"   : "?";
        serial_printf("[RTL8168] Link UP: %sMbps %s-duplex\n",
                      speed, (phy & PHYSTAT_FULL_DUP) ? "full" : "half");
    } else {
        serial_printf("[RTL8168] Link currently DOWN (waiting for cable/negotiation)...\n");
    }

    serial_printf("===========================================\n\n");
    return true;
}

bool rtl8168_is_present(void) {
    return rtl_present;
}

void rtl8168_get_mac(uint8_t *mac_out) {
    for (int i = 0; i < 6; i++) mac_out[i] = mac_address[i];
}

bool rtl8168_send_packet(const void *data, uint32_t len) {
    if (!rtl_mmio || !rtl_present) return false;
    if (len > RTL_BUF_SIZE - 4) return false;

    uint32_t cpu_flags;
    asm volatile("pushf; pop %0; cli" : "=r"(cpu_flags));

    /* Wait for this slot's previous transmission to finish (OWN bit clear) */
    int timeout = 500000;
    while ((tx_ring[cur_tx].opts1 & DESC_OWN) && timeout-- > 0) {
        /* spin */
    }
    if (timeout <= 0) {
        serial_printf("[RTL8168] TX slot %u timeout, buffer busy!\n", cur_tx);
        asm volatile("push %0; popf" :: "r"(cpu_flags));
        return false;
    }

    /* Copy frame into this slot's fixed DMA buffer */
    uint8_t *dst = tx_buf[cur_tx];
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];

    uint32_t tx_len = len;
    if (tx_len < 60) { /* pad to minimum Ethernet frame size */
        for (uint32_t i = tx_len; i < 60; i++) dst[i] = 0;
        tx_len = 60;
    }

    uint32_t eor = (cur_tx == NUM_TX_DESC - 1) ? DESC_EOR : 0;
    tx_ring[cur_tx].opts2 = 0;
    tx_ring[cur_tx].opts1 = DESC_OWN | eor | DESC_FS | DESC_LS | (tx_len & DESC_LEN_MASK);

    /* Kick the normal-priority TX queue so the NIC picks the descriptor up */
    RTL_W8(R_TX_POLL, TXPOLL_NPQ);

    cur_tx = (cur_tx + 1) % NUM_TX_DESC;

    asm volatile("push %0; popf" :: "r"(cpu_flags));
    return true;
}
