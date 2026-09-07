/* =============================================================================
 * ZeruX OS — RTL8139 PCI Network Interface Controller (NIC) Driver
 * File: kernel/drivers/rtl8139.c
 * =============================================================================
 */

#include "rtl8139.h"
#include "pci.h"
#include "ports.h"
#include "serial.h"
#include "pmm.h"
#include "irq.h"
#include <stddef.h>

/* RTL8139 Registers */
#define RTL8139_MAC_ADDR         0x00
#define RTL8139_MAR              0x08
#define RTL8139_TX_STATUS0       0x10
#define RTL8139_TX_ADDR0         0x20
#define RTL8139_RX_BUF           0x30
#define RTL8139_COMMAND          0x37
#define RTL8139_CAPR             0x38  /* Current Address of Packet Read */
#define RTL8139_CBR              0x3A
#define RTL8139_INTR_MASK        0x3C
#define RTL8139_INTR_STATUS      0x3E
#define RTL8139_TX_CONFIG        0x40
#define RTL8139_RX_CONFIG        0x44
#define RTL8139_TIMER            0x48
#define RTL8139_MISC             0x4C
#define RTL8139_CONFIG1          0x52
#define RTL8139_CONFIG0          0x51
#define RTL8139_HLT_CLK          0x5B
#define RTL8139_BMCR             0x62
#define RTL8139_BMSR             0x64

/* CR (Command Register) bits */
#define CR_RST      0x10
#define CR_RE       0x08
#define CR_TE       0x04
#define CR_BUFE     0x01

/* ISR/IMR bits */
#define ROK         0x01
#define RER         0x02
#define TOK         0x04
#define TER         0x08
#define RXOVW       0x10
#define PUN_LINKCHG 0x20
#define RX_FIFO_OVR 0x40
#define SERR        0x80

/* RCR bits */
#define RCR_AAP     (1 << 0) /* Accept All Packets */
#define RCR_APM     (1 << 1) /* Accept Physical Match Packets */
#define RCR_AM      (1 << 2) /* Accept Multicast Packets */
#define RCR_AB      (1 << 3) /* Accept Broadcast Packets */
#define RCR_WRAP    (1 << 7) /* Wrap packets at end of buffer */

static uint32_t rtl_io_base = 0;
static uint8_t  rtl_irq     = 0;
static uint8_t  mac_address[6];
static uint8_t *rx_buffer   = NULL;
static uint32_t current_rx_ptr = 0;

static uint8_t *tx_buffers[4];
static uint8_t  tx_cur_buf = 0;

static void rtl8139_handler(registers_t *regs) {
    (void)regs;
    if (rtl_io_base == 0) return;

    uint16_t status = inw(rtl_io_base + RTL8139_INTR_STATUS);
    if (!status) return;

    /* Acknowledge interrupts */
    outw(rtl_io_base + RTL8139_INTR_STATUS, status);

    if (status & TOK) {
        /* Packet transmitted successfully, no log needed to mimic Linux */
    }
    
    if (status & ROK) {
        /* Read packets until we catch up to the current write pointer (CBR) */
        while ((inb(rtl_io_base + RTL8139_COMMAND) & CR_BUFE) == 0) {
            uint32_t rx_ptr = current_rx_ptr;
            
            uint16_t pkt_status = *(uint16_t*)(rx_buffer + rx_ptr);
            uint16_t pkt_len    = *(uint16_t*)(rx_buffer + rx_ptr + 2);
            
            if (pkt_status & 0x01) { /* ROK bit in packet header */
                uint8_t *packet = rx_buffer + rx_ptr + 4;
                uint32_t data_len = pkt_len - 4; /* Exclude CRC */
                
                extern void net_receive_packet(const uint8_t *packet, uint32_t len);
                net_receive_packet(packet, data_len);
            }
            
            /* Advance pointer. Packets are padded to 4-byte boundaries */
            current_rx_ptr = (current_rx_ptr + pkt_len + 4 + 3) & ~3;
            if (current_rx_ptr >= 8192) {
                current_rx_ptr -= 8192;
            }
            
            /* Update CAPR (hardware pointer). Note: must subtract 16 per RTL spec */
            outw(rtl_io_base + RTL8139_CAPR, current_rx_ptr - 16);
        }
    }
}

void rtl8139_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — RTL8139 Network Driver\n");
    serial_printf("===========================================\n");

    /* Find RTL8139 PCI Device (Vendor 0x10EC, Device 0x8139) */
    pci_device_t *pdev = pci_find_device(0x10EC, 0x8139);
    if (!pdev) {
        serial_printf("[RTL8139] ERROR: Device not found on PCI bus.\n");
        return;
    }

    rtl_io_base = pdev->bar0 & ~3;
    rtl_irq     = pdev->interrupt_line;

    serial_printf("[RTL8139] Found Device at I/O Base: 0x%x, IRQ: %u\n", rtl_io_base, rtl_irq);

    /* Enable PCI Bus Mastering and I/O space access */
    uint32_t pci_cmd = pci_config_read32(pdev->bus, pdev->device, pdev->function, 0x04);
    pci_cmd |= (1 << 2); /* Bus Master */
    pci_cmd |= (1 << 0); /* I/O Space */
    pci_config_write32(pdev->bus, pdev->device, pdev->function, 0x04, pci_cmd);

    /* Turn on the RTL8139 (Write 0x00 to Config1 register) */
    outb(rtl_io_base + RTL8139_CONFIG1, 0x00);

    /* 2. Soft Reset */
    outb(rtl_io_base + RTL8139_COMMAND, CR_RST);
    int reset_timeout = 100000;
    while ((inb(rtl_io_base + RTL8139_COMMAND) & CR_RST) != 0) {
        if (--reset_timeout == 0) {
            serial_printf("[RTL8139] Reset timeout!\n");
            return;
        }
    }
    serial_printf("[RTL8139] Software reset completed.\n");

    /* Read MAC Address */
    serial_printf("[RTL8139] MAC Address: ");
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        mac_address[i] = inb(rtl_io_base + RTL8139_MAC_ADDR + i);
        uint8_t b = mac_address[i];
        serial_printf("%c%c%s", hex[b >> 4], hex[b & 0xF], i == 5 ? "\n" : ":");
    }

    /* Allocate RX Buffer (needs to be 8K + 16 bytes = approx 3 pages) */
    rx_buffer = (uint8_t*)pmm_alloc_blocks(3);
    if (!rx_buffer) {
        serial_printf("[RTL8139] CRITICAL ERROR: Could not allocate RX Buffer!\n");
        return;
    }
    outl(rtl_io_base + RTL8139_RX_BUF, (uint32_t)rx_buffer);

    /* Allocate TX Buffers (4 buffers, each 2K bytes) -> 2 pages total */
    uint8_t *tx_mem = (uint8_t*)pmm_alloc_blocks(2);
    for (int i = 0; i < 4; i++) {
        tx_buffers[i] = tx_mem + (i * 2048);
    }

    /* Configure Receive Register */
    /* Accept Broadcast, Multicast, Physical Match. WRAP=1 allows packet wrap around */
    outl(rtl_io_base + RTL8139_RX_CONFIG, RCR_AB | RCR_AM | RCR_APM | RCR_WRAP);

    /* Set Interrupt Mask (Enable ROK, TOK, and RXOVW) */
    outw(rtl_io_base + RTL8139_INTR_MASK, ROK | TOK | RXOVW);

    /* Hook IRQ */
    irq_install_handler(rtl_irq, rtl8139_handler);
    serial_printf("[RTL8139] Hooked IRQ %u.\n", rtl_irq);

    /* Enable Receive and Transmit */
    outb(rtl_io_base + RTL8139_COMMAND, CR_RE | CR_TE);
    serial_printf("[RTL8139] RX/TX Enabled. Initialization complete.\n");
    serial_printf("===========================================\n\n");
}

void rtl8139_get_mac(uint8_t *mac_out) {
    for (int i = 0; i < 6; i++) mac_out[i] = mac_address[i];
}

bool rtl8139_send_packet(const void *data, uint32_t len) {
    if (rtl_io_base == 0) return false;
    if (len > 1792) return false;

    /* Claim a TX slot safely (atomic) */
    uint32_t cpu_flags;
    asm volatile("pushf; pop %0; cli" : "=r"(cpu_flags));
    uint8_t slot = tx_cur_buf;
    tx_cur_buf = (tx_cur_buf + 1) % 4;
    asm volatile("push %0; popf" :: "r"(cpu_flags));

    /* Wait for the claimed slot to complete previous transmission (TOK or OWN bit) */
    uint32_t status_reg = rtl_io_base + RTL8139_TX_STATUS0 + (slot * 4);
    int timeout = 500000;
    
    /* We wait with interrupts ENABLED (unless called with IF=0), preventing full system freeze */
    while ((inl(status_reg) & 0x2000) == 0 && timeout-- > 0) {
        /* Wait */
    }
    
    if (timeout <= 0) {
        serial_printf("[RTL8139] TX slot %u timeout, dropping packet!\n", slot);
        /* We do not revert tx_cur_buf. The stuck slot is abandoned, next send uses next slot. */
        return false;
    }

    /* Copy data to current TX buffer */
    uint8_t *dst = tx_buffers[slot];
    const uint8_t *src = (const uint8_t*)data;
    uint32_t copy_len = len;
    
    for (uint32_t i = 0; i < copy_len; i++) {
        dst[i] = src[i];
    }
    
    /* Pad to minimum Ethernet frame size (60 bytes) */
    if (len < 60) {
        for (uint32_t i = len; i < 60; i++) {
            dst[i] = 0;
        }
        len = 60;
    }

    /* Write TX physical address */
    outl(rtl_io_base + RTL8139_TX_ADDR0 + (slot * 4), (uint32_t)dst);

    /* Write length to trigger transmission */
    outl(rtl_io_base + RTL8139_TX_STATUS0 + (slot * 4), len);

    return true;
}
