/* =============================================================================
 * ZeruX OS — FAT32 File System Driver Header
 * File: kernel/include/fat32.h
 * =============================================================================
 *
 * FAT32 (File Allocation Table 32):
 *   - Windows, Linux ve macOS tarafından yerleşik desteklenen dosya sistemi.
 *   - BPB (BIOS Parameter Block) parse etme, FAT Tablosu okuma,
 *     Cluster Chain (Küme Zinciri) takibi ve 8.3 Dizin yapısı desteği.
 * =============================================================================
 */

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

/* FAT Directory Attributes */
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

#pragma pack(push, 1)
/* FAT32 BPB (BIOS Parameter Block - 512 Baytlık Önyükleme Sektörü) */
typedef struct {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    /* FAT32 Özgü Alanlar */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  win_nt_flags;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type_string[8];
} fat32_bpb_t;
#pragma pack(pop)

#pragma pack(push, 1)
/* FAT32 Directory Entry (32 Baytlık Dizin Girdisi)
 * Microsoft FAT32 Specification - exact offset layout:
 *   0-10: DIR_Name        (11 bytes, 8.3 format)
 *   11:   DIR_Attr        (1 byte)
 *   12:   DIR_NTRes       (1 byte)
 *   13:   DIR_CrtTimeTenth(1 byte)
 *   14-15:DIR_CrtTime     (2 bytes)
 *   16-17:DIR_CrtDate     (2 bytes)
 *   18-19:DIR_LstAccDate  (2 bytes) ← last access date
 *   20-21:DIR_FstClusHI   (2 bytes) ← first_cluster_high
 *   22-23:DIR_WrtTime     (2 bytes)
 *   24-25:DIR_WrtDate     (2 bytes)
 *   26-27:DIR_FstClusLO   (2 bytes) ← first_cluster_low
 *   28-31:DIR_FileSize    (4 bytes) ← file_size
 *   Total: 32 bytes exactly
 */
typedef struct {
    char     name[11];           /* 0-10: 8.3 Dosya İsmi ("WELCOME TXT") */
    uint8_t  attr;               /* 11: Dosya Özniteliği */
    uint8_t  nt_reserved;        /* 12 */
    uint8_t  creation_time_tenth;/* 13 */
    uint16_t creation_time;      /* 14-15 */
    uint16_t creation_date;      /* 16-17 */
    uint16_t last_access_date;   /* 18-19 = DIR_LstAccDate */
    uint16_t first_cluster_high; /* 20-21 = DIR_FstClusHI */
    uint16_t write_time;         /* 22-23 */
    uint16_t write_date;         /* 24-25 */
    uint16_t first_cluster_low;  /* 26-27 = DIR_FstClusLO */
    uint32_t file_size;          /* 28-31 = DIR_FileSize */
} fat32_dir_entry_t;
#pragma pack(pop)

/* Public API Bildirimleri */
void     fat32_init(void);
void     fat32_run_test_suite(void);
uint32_t fat32_create(uint32_t dir_cluster, const char *name);
uint32_t fat32_mkdir(uint32_t dir_cluster, const char *name);
int      fat32_unlink(uint32_t dir_cluster, const char *name);
/* Root cluster accessor for shell use */
uint32_t fat32_get_root_cluster(void);
uint32_t fat32_find_entry(uint32_t dir_cluster, const char *name);

#endif /* FAT32_H */
