/* =============================================================================
 * ZeruX OS — Primary ATA/IDE Controller Driver Header (PIO Mode)
 * File: kernel/include/ata.h
 * =============================================================================
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_SECTOR_SIZE 512

/* Primary ATA Bus I/O Port Base (0x1F0 - 0x1F7) */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR_FEAT     0x1F1
#define ATA_PRIMARY_SEC_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6
#define ATA_PRIMARY_COMMAND_STAT 0x1F7

/* ATA Status Register Bits (Port 0x1F7) */
#define ATA_SR_BSY  0x80    /* Busy bit */
#define ATA_SR_DRDY 0x40    /* Drive Ready bit */
#define ATA_SR_DF   0x20    /* Drive Fault bit */
#define ATA_SR_DRQ  0x08    /* Data Request Ready bit */
#define ATA_SR_ERR  0x01    /* Error bit */

/* ATA Commands */
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_CACHE_FLUSH   0xE7

/* Public API Bildirimleri */
void ata_init(void);
int  ata_read_sector(uint32_t lba, uint8_t *buf);
int  ata_write_sector(uint32_t lba, const uint8_t *buf);
void ata_run_test_suite(void);

#endif /* ATA_H */
