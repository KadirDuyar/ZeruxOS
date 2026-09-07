/* =============================================================================
 * ZeruX OS — Primary ATA/IDE Hard Disk Controller Driver (PIO Mode)
 * File: kernel/drivers/ata.c
 * =============================================================================
 *
 * Primary ATA Bus Architecture:
 *   - I/O Ports: 0x1F0 (Data) .. 0x1F7 (Command/Status)
 *   - PIO 16-bit word transfers via inw/outw primitives
 *   - Supports LBA28 Sector Addressing up to 128 GB disks
 * =============================================================================
 */

#include "ata.h"
#include "ports.h"
#include "serial.h"
#include "timer.h"

/* Helper: 400ns delay for ATA controller */
static inline void ata_delay(void) {
    inb(0x3F6);
    inb(0x3F6);
    inb(0x3F6);
    inb(0x3F6);
}

/* Helper: ATA BSY (Busy) durumunun temizlenmesini bekle (Timeout'lu) */
static int ata_wait_bsy(void) {
    uint32_t timeout = 50000000;
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_COMMAND_STAT);
        if (status == 0xFF) return -1; /* Floating bus (no device attached) */
        if (!(status & ATA_SR_BSY)) {
            return 0; /* Ready */
        }
    }

    serial_printf("[ATA ERROR] BSY timeout!\n");
    return -1;
}

/* Helper: ATA DRQ (Data Request) durumunun hazır olmasını bekle (Timeout'lu) */
static int ata_wait_drq(void) {
    uint32_t timeout = 50000000;
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_COMMAND_STAT);
        if (status == 0xFF) return -1; /* Floating bus */
        if (status & ATA_SR_ERR) {
            serial_printf("[ATA ERROR] ATA Controller reported Hardware Error (ERR bit set)!\n");
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0; /* Ready for Data Transfer */
        }
    }
    serial_printf("[ATA ERROR] DRQ timeout!\n");
    return -1;
}

/* =============================================================================
 * ata_init() — Primary Master Hard Disk IDENTIFY Taraması
 * =============================================================================
 */
void ata_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Primary ATA/IDE Controller (PIO Mode)\n");
    serial_printf("===========================================\n");

    /* 1. Drive 0 (Master) Seç */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);
    outb(ATA_PRIMARY_SEC_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);

    /* 2. IDENTIFY Komutunu Gönder (0xEC) */
    outb(ATA_PRIMARY_COMMAND_STAT, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PRIMARY_COMMAND_STAT);
    if (status == 0 || status == 0xFF) {
        serial_printf("[ATA] Primary Master Device: NOT PRESENT (Status 0x%02x)\n\n", status);
        return;
    }

    if (ata_wait_bsy() != 0) {
        serial_printf("[ATA] Primary Master Device: BSY wait failed (no device)\n\n");
        return;
    }

    status = inb(ATA_PRIMARY_COMMAND_STAT);
    if (status & ATA_SR_ERR) {
        serial_printf("[ATA] Primary Master Device: IDENTIFY Error (Status 0x%x)\n\n", status);
        return;
    }

    if (ata_wait_drq() != 0) {
        serial_printf("[ATA] Primary Master Device: DRQ wait failed\n\n");
        return;
    }

    /* 3. 256 Word (512 Bayt) IDENTIFY Tamponunu Oku */
    uint16_t identify_buf[256];
    for (int i = 0; i < 256; i++) {
        identify_buf[i] = inw(ATA_PRIMARY_DATA);
    }

    uint32_t total_sectors = ((uint32_t)identify_buf[61] << 16) | identify_buf[60];

    serial_printf("[ATA] Primary Master Hard Disk Found!\n");
    serial_printf("[ATA] Total LBA28 Sectors : %u (%u MB / %u KB)\n",
                  total_sectors, total_sectors * 512 / (1024 * 1024), total_sectors * 512 / 1024);
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * ata_read_sector() — LBA Adresinden 1 Sektör (512 Bayt) Oku
 * =============================================================================
 */
int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (!buf) return -1;

    if (ata_wait_bsy() != 0) return -1;

    /* Select Drive 0 (Master) + Highest 4 bits of LBA */
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Send READ SECTORS Command (0x20) */
    outb(ATA_PRIMARY_COMMAND_STAT, ATA_CMD_READ_SECTORS);
    ata_delay();

    if (ata_wait_bsy() != 0) {
        serial_printf("[ATA FATAL] BSY Timeout in ata_read_sector!\n");
        return -1;
    }
    if (ata_wait_drq() != 0) {
        serial_printf("[ATA FATAL] DRQ Timeout in ata_read_sector!\n");
        return -1;
    }

    /* Read 256 words (512 bytes) from Data Port (0x1F0) */
    uint16_t *ptr = (uint16_t*)buf;
    __asm__ volatile ("cli");
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PRIMARY_DATA);
    }
    __asm__ volatile ("sti");

    return 0;
}

/* =============================================================================
 * ata_write_sector() — LBA Adresine 1 Sektör (512 Bayt) Yaz
 * =============================================================================
 */
int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (!buf) return -1;

    if (ata_wait_bsy() != 0) return -1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, 1);
    outb(ATA_PRIMARY_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* Send WRITE SECTORS Command (0x30) */
    outb(ATA_PRIMARY_COMMAND_STAT, ATA_CMD_WRITE_SECTORS);
    ata_delay();

    if (ata_wait_bsy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;

    /* Write 256 words (512 bytes) to Data Port (0x1F0) */
    const uint16_t *ptr = (const uint16_t*)buf;
    __asm__ volatile ("cli");
    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, ptr[i]);
    }
    __asm__ volatile ("sti");

    /* Cache Flush (0xE7) */
    outb(ATA_PRIMARY_COMMAND_STAT, ATA_CMD_CACHE_FLUSH);
    ata_delay();
    if (ata_wait_bsy() != 0) return -1;

    return 0;
}

/* =============================================================================
 * ata_run_test_suite() — Adım 4.1 ATA Sürücü Doğrulama Testi
 * =============================================================================
 */
void ata_run_test_suite(void) {
    serial_printf("[ATA TEST] Running Primary Hard Disk sector read test suite...\n");

    uint8_t sector_buf[512];
    if (ata_read_sector(0, sector_buf) == 0) {
        uint16_t magic = (sector_buf[511] << 8) | sector_buf[510];
        serial_printf("[ATA TEST] Read Sector 0 (MBR Sector) via Primary ATA PIO Driver!\n");
        serial_printf("[ATA TEST] MBR Magic Signature Check: 0x%x (Expected 0xAA55)\n", magic);

        if (magic == 0xAA55) {
            serial_printf("\n=======================================================\n");
            serial_printf(" [ATA TEST PASSED] Primary ATA/IDE Hard Disk Driver Verified!\n");
            serial_printf("   - Sector 0 (MBR Sector) Read Successfully\n");
            serial_printf("   - Bootloader Signature Magic 0xAA55 Verified\n");
            serial_printf("=======================================================\n\n");
        } else {
            serial_printf("[ATA TEST FAILED] Invalid MBR Signature!\n");
        }
    } else {
        serial_printf("[ATA TEST FAILED] Failed to read Sector 0!\n");
    }
}
