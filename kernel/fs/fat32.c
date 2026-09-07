/* =============================================================================
 * ZeruX OS — REAL FAT32 File System Driver (Read + Write)
 * File: kernel/fs/fat32.c
 * =============================================================================
 *
 * Desteklenen İşlemler:
 *   READ  : Cluster chain traversal, 8.3 name parse, finddir/readdir
 *   WRITE : fat32_file_write, fat32_create, fat32_mkdir, fat32_unlink
 *
 * Yazma Akisi:
 *   1. fat32_alloc_cluster()          -> Bos cluster bul, FAT'a EOF yaz
 *   2. fat32_write_fat_entry()        -> Cluster zinciri guncelle
 *   3. fat32_write_dir_entry_to_dir() -> 32-byte dizin girdisi diske yaz
 *   4. fat32_file_write()             -> Veri cluster'larina yaz
 * =============================================================================
 */

#include "fat32.h"
#include "vfs.h"
#include "ata.h"
#include "serial.h"
#include "kheap.h"

/*
 * LBA 512 = 256 KB offset. (Kernel starts at LBA 5 and can take up to ~507 LBAs)
 */
#define FAT32_VOLUME_START_LBA 512
#define FAT32_MAX_NODES        64
#define FAT32_EOF              0x0FFFFFFF
#define FAT32_FREE             0x00000000

/* ============================================================
 * Global State
 * ============================================================ */
static fat32_bpb_t g_bpb;
static uint32_t    g_first_fat_lba  = 0;
static uint32_t    g_first_data_lba = 0;
static uint32_t    g_total_clusters = 0;

typedef struct {
    char     name[VFS_MAX_NAME_LEN];
    uint32_t first_cluster;
    uint32_t size;
    uint8_t  attr;
    uint32_t entry_dir_cluster;
    uint32_t entry_index;
} fat32_real_file_t;

static fat32_real_file_t g_fat32_files[FAT32_MAX_NODES];
static vfs_node_t        g_fat32_vnodes[FAT32_MAX_NODES];
static uint32_t          g_fat32_node_count = 0;

/* ============================================================
 * String Helpers
 * ============================================================ */

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static int kstrcasecmp(const char *s1, const char *s2) {
    while (*s1 && (to_upper(*s1) == to_upper(*s2))) { s1++; s2++; }
    return (int)(to_upper(*s1)) - (int)(to_upper(*s2));
}

static char* kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (i = 0; i < n - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
    return dest;
}

static size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}



/* ============================================================
 * 8.3 Name Helpers
 * ============================================================ */
static void fat32_parse_83_name(const char *fat_name, char *out_name) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (fat_name[i] != ' ') out_name[pos++] = fat_name[i];
    }
    if (fat_name[8] != ' ') {
        out_name[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (fat_name[i] != ' ') out_name[pos++] = fat_name[i];
        }
    }
    out_name[pos] = '\0';
}

static void fat32_name_to_83(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int len = (int)kstrlen(name);
    int dot = -1;
    for (int i = 0; i < len; i++) {
        if (name[i] == '.') { dot = i; break; }
    }
    int name_len = (dot >= 0) ? dot : len;
    if (name_len > 8) name_len = 8;
    for (int i = 0; i < name_len; i++) out[i] = to_upper(name[i]);
    if (dot >= 0) {
        int ext_len = len - dot - 1;
        if (ext_len > 3) ext_len = 3;
        for (int i = 0; i < ext_len; i++) out[8 + i] = to_upper(name[dot + 1 + i]);
    }
}

/* ============================================================
 * FAT Table Read/Write
 * ============================================================ */
static uint32_t fat32_read_fat_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_first_fat_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;
    uint8_t  sector_buf[512];
    if (ata_read_sector(fat_sector, sector_buf) != 0) return FAT32_EOF;
    uint32_t value = *(uint32_t*)&sector_buf[ent_offset];
    return value & 0x0FFFFFFF;
}

static int fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_first_fat_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;
    uint8_t  sector_buf[512];
    if (ata_read_sector(fat_sector, sector_buf) != 0) return -1;
    uint32_t *entry = (uint32_t*)&sector_buf[ent_offset];
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
    if (ata_write_sector(fat_sector, sector_buf) != 0) return -1;
    if (g_bpb.num_fats >= 2) {
        uint32_t fat2_sector = fat_sector + g_bpb.fat_size_32;
        ata_write_sector(fat2_sector, sector_buf);
    }
    return 0;
}

static uint32_t g_fat32_next_free_cluster = 2;

static uint32_t fat32_alloc_cluster(void) {
    for (uint32_t c = g_fat32_next_free_cluster; c < g_total_clusters + 2; c++) {
        if (fat32_read_fat_entry(c) == FAT32_FREE) {
            fat32_write_fat_entry(c, FAT32_EOF);
            g_fat32_next_free_cluster = c + 1;
            return c;
        }
    }
    for (uint32_t c = 2; c < g_fat32_next_free_cluster; c++) {
        if (fat32_read_fat_entry(c) == FAT32_FREE) {
            fat32_write_fat_entry(c, FAT32_EOF);
            g_fat32_next_free_cluster = c + 1;
            return c;
        }
    }
    serial_printf("[FAT32] No free clusters!\n");
    return 0;
}

static void fat32_free_chain(uint32_t cluster) {
    uint32_t limit = g_bpb.sectors_per_cluster ? (g_bpb.total_sectors_32 / g_bpb.sectors_per_cluster + 2) : 100000;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && limit--) {
        uint32_t next = fat32_read_fat_entry(cluster);
        fat32_write_fat_entry(cluster, FAT32_FREE);
        cluster = next;
    }
}

static uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    return g_first_data_lba + (cluster - 2) * g_bpb.sectors_per_cluster;
}

static void fat32_zero_cluster(uint32_t cluster) {
    uint8_t  zero[512];
    uint32_t lba = fat32_cluster_to_lba(cluster);
    for (int i = 0; i < 512; i++) zero[i] = 0;
    for (uint32_t s = 0; s < g_bpb.sectors_per_cluster; s++) {
        ata_write_sector(lba + s, zero);
    }
}

/* ============================================================
 * Directory Walker
 * mode 0 = find by index (readdir)
 * mode 1 = find by name  (finddir)
 * mode 2 = find empty slot (write new entry)
 * mode 3 = unlink (mark 0xE5)
 * ============================================================ */
typedef struct {
    int               mode;
    uint32_t          target_index;
    const char       *target_name;
    fat32_dir_entry_t *out_entry;
    fat32_dir_entry_t *write_entry;
    uint32_t          result_lba;
    uint32_t          result_byte_offset;
} dir_walk_ctx_t;

static int fat32_walk_dir(uint32_t dir_cluster, dir_walk_ctx_t *ctx) {
    uint32_t current_cluster = dir_cluster;
    uint32_t current_index   = 0;
    uint8_t  sector_buf[512];
    uint32_t limit = g_bpb.sectors_per_cluster ? (g_bpb.total_sectors_32 / g_bpb.sectors_per_cluster + 2) : 100000;

    while (current_cluster < 0x0FFFFFF8 && limit--) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);

        for (uint32_t sec = 0; sec < g_bpb.sectors_per_cluster; sec++) {
            /* Retry ATA read up to 3 times — QEMU timing issues */
            int ata_ok = 0;
            for (int retry = 0; retry < 3; retry++) {
                if (ata_read_sector(lba + sec, sector_buf) == 0) { ata_ok = 1; break; }
            }
            if (!ata_ok) {
                serial_printf("[FAT32] ATA read FAILED: LBA=%u (cluster=%u)\n", lba + sec, current_cluster);
                return 0;
            }

            for (int e = 0; e < 16; e++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(sector_buf + e * 32);

                if (ctx->mode == 2) {
                    if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5) {
                        if (ctx->write_entry) {
                            for (int i = 0; i < 32; i++)
                                ((uint8_t*)entry)[i] = ((uint8_t*)ctx->write_entry)[i];
                            ata_write_sector(lba + sec, sector_buf);
                        }
                        return 1;
                    }
                    continue;
                }

                if (entry->name[0] == 0x00) {
                    serial_printf("[FAT32] walk: end-of-dir at e=%d lba=%u cluster=%u\n", e, lba+sec, current_cluster);
                    return 0;
                }
                if ((uint8_t)entry->name[0] == 0xE5) { continue; } /* Silinmiş dosyaları index'e sayma */
                if (entry->attr == FAT_ATTR_LFN) continue;
                if (entry->attr & FAT_ATTR_VOLUME_ID) continue;

                char parsed_name[VFS_MAX_NAME_LEN];
                fat32_parse_83_name(entry->name, parsed_name);

                if (ctx->mode == 0) {
                    if (current_index == ctx->target_index) {
                        *ctx->out_entry = *entry;
                        return 1;
                    }
                    current_index++;
                } else if (ctx->mode == 1) {
                    if (kstrcasecmp(parsed_name, ctx->target_name) == 0) {
                        if (ctx->out_entry) *ctx->out_entry = *entry;
                        ctx->result_lba         = lba + sec;
                        ctx->result_byte_offset = (uint32_t)(e * 32);
                        return 1;
                    }
                    current_index++;
                } else if (ctx->mode == 3) {
                    if (kstrcasecmp(parsed_name, ctx->target_name) == 0) {
                        entry->name[0] = 0xE5;
                        ata_write_sector(lba + sec, sector_buf);
                        return 1;
                    }
                    current_index++;
                }
            }
        }

        uint32_t next = fat32_read_fat_entry(current_cluster);
        if (next >= 0x0FFFFFF8) {
            /* End of chain */
            if (ctx->mode == 2 && ctx->write_entry) {
                /* Extend directory with a new cluster */
                uint32_t new_cluster = fat32_alloc_cluster();
                if (new_cluster == 0) return 0;
                fat32_write_fat_entry(current_cluster, new_cluster);
                fat32_zero_cluster(new_cluster);
                uint8_t new_sec[512];
                uint32_t new_lba = fat32_cluster_to_lba(new_cluster);
                if (ata_read_sector(new_lba, new_sec) != 0) return 0;
                for (int i = 0; i < 32; i++)
                    new_sec[i] = ((uint8_t*)ctx->write_entry)[i];
                ata_write_sector(new_lba, new_sec);
                return 1;
            }
            break;
        }
        current_cluster = next;
    }
    return 0;
}

static int fat32_write_dir_entry_to_dir(uint32_t dir_cluster, fat32_dir_entry_t *entry) {
    dir_walk_ctx_t ctx;
    ctx.mode        = 2;
    ctx.write_entry = entry;
    ctx.out_entry   = (void*)0;
    ctx.target_name = (void*)0;
    ctx.target_index = 0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    return fat32_walk_dir(dir_cluster, &ctx);
}

static void fat32_update_dir_entry(uint32_t dir_cluster, const char *name, uint32_t new_size, uint32_t first_cluster) {
    uint32_t current_cluster = dir_cluster;
    uint8_t  sector_buf[512];
    uint32_t limit = g_bpb.sectors_per_cluster ? (g_bpb.total_sectors_32 / g_bpb.sectors_per_cluster + 2) : 100000;
    while (current_cluster < 0x0FFFFFF8 && limit--) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);
        for (uint32_t sec = 0; sec < g_bpb.sectors_per_cluster; sec++) {
            if (ata_read_sector(lba + sec, sector_buf) != 0) return;
            for (int e = 0; e < 16; e++) {
                fat32_dir_entry_t *entry = (fat32_dir_entry_t*)(sector_buf + e * 32);
                if (entry->name[0] == 0x00) return;
                if ((uint8_t)entry->name[0] == 0xE5) continue;
                if (entry->attr == FAT_ATTR_LFN) continue;
                if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
                char pname[VFS_MAX_NAME_LEN];
                fat32_parse_83_name(entry->name, pname);
                if (kstrcasecmp(pname, name) == 0) {
                    entry->file_size = new_size;
                    entry->first_cluster_high = (uint16_t)(first_cluster >> 16);
                    entry->first_cluster_low  = (uint16_t)(first_cluster & 0xFFFF);
                    ata_write_sector(lba + sec, sector_buf);
                    return;
                }
            }
        }
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
}

/* ============================================================
 * File Read
 * ============================================================ */
static int32_t fat32_file_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !buffer) return -1;
    fat32_real_file_t *file = (fat32_real_file_t*)node->device_data;
    if (!file) return -1;
    if (offset >= file->size) return 0;
    uint32_t bytes_to_read = file->size - offset;
    if (size < bytes_to_read) bytes_to_read = size;
    uint32_t current_cluster = file->first_cluster;
    uint32_t cluster_size    = g_bpb.sectors_per_cluster * 512;
    uint32_t clusters_to_skip = offset / cluster_size;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        if (current_cluster >= 0x0FFFFFF8) return 0;
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
    uint32_t offset_in_cluster = offset % cluster_size;
    uint32_t bytes_read = 0;
    uint8_t  sector_buf[512];
    uint32_t limit = g_bpb.sectors_per_cluster ? (g_bpb.total_sectors_32 / g_bpb.sectors_per_cluster + 2) : 100000;
    while (bytes_read < bytes_to_read && current_cluster < 0x0FFFFFF8 && limit--) {
        uint32_t start_lba = fat32_cluster_to_lba(current_cluster);
        for (uint32_t sec = 0; sec < g_bpb.sectors_per_cluster && bytes_read < bytes_to_read; sec++) {
            if (offset_in_cluster >= 512) { offset_in_cluster -= 512; continue; }
            if (ata_read_sector(start_lba + sec, sector_buf) != 0) return bytes_read;
            uint32_t to_copy = 512 - offset_in_cluster;
            if (bytes_to_read - bytes_read < to_copy) to_copy = bytes_to_read - bytes_read;
            for (uint32_t i = 0; i < to_copy; i++)
                buffer[bytes_read + i] = sector_buf[offset_in_cluster + i];
            bytes_read += to_copy;
            offset_in_cluster = 0;
        }
        current_cluster = fat32_read_fat_entry(current_cluster);
    }
    return bytes_read;
}

/* ============================================================
 * File Write
 * ============================================================ */
static int32_t fat32_file_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    if (!node || !buffer || size == 0) return -1;
    fat32_real_file_t *file = (fat32_real_file_t*)node->device_data;
    if (!file) return -1;

    uint32_t cluster_size = g_bpb.sectors_per_cluster * 512;
    uint32_t written      = 0;

    /* Allocate first cluster if file is empty */
    if (file->first_cluster < 2) {
        uint32_t new_c = fat32_alloc_cluster();
        if (new_c == 0) return -1;
        fat32_zero_cluster(new_c);
        file->first_cluster = new_c;
        node->inode         = new_c;
    }

    uint32_t current_cluster = file->first_cluster;

    /* Navigate to the cluster containing offset */
    uint32_t cluster_idx = offset / cluster_size;
    for (uint32_t i = 0; i < cluster_idx; i++) {
        uint32_t next = fat32_read_fat_entry(current_cluster);
        if (next >= 0x0FFFFFF8) {
            uint32_t new_c = fat32_alloc_cluster();
            if (new_c == 0) return (int32_t)written;
            fat32_zero_cluster(new_c);
            fat32_write_fat_entry(current_cluster, new_c);
            current_cluster = new_c;
        } else {
            current_cluster = next;
        }
    }

    uint32_t offset_in_cluster = offset % cluster_size;
    uint32_t limit = g_bpb.sectors_per_cluster ? (g_bpb.total_sectors_32 / g_bpb.sectors_per_cluster + 2) : 100000;

    while (written < size && limit--) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);
        for (uint32_t sec = 0; sec < g_bpb.sectors_per_cluster && written < size; sec++) {
            if (offset_in_cluster >= 512) { offset_in_cluster -= 512; continue; }
            uint8_t sector_buf[512];
            ata_read_sector(lba + sec, sector_buf);
            uint32_t to_write = 512 - offset_in_cluster;
            if (size - written < to_write) to_write = size - written;
            for (uint32_t i = 0; i < to_write; i++)
                sector_buf[offset_in_cluster + i] = buffer[written + i];
            if (ata_write_sector(lba + sec, sector_buf) != 0) return (int32_t)written;
            written += to_write;
            offset_in_cluster = 0;
        }
        if (written < size) {
            uint32_t next = fat32_read_fat_entry(current_cluster);
            if (next >= 0x0FFFFFF8) {
                uint32_t new_c = fat32_alloc_cluster();
                if (new_c == 0) break;
                fat32_zero_cluster(new_c);
                fat32_write_fat_entry(current_cluster, new_c);
                current_cluster = new_c;
            } else {
                current_cluster = next;
            }
        }
    }

    uint32_t new_size = offset + written;
    if (new_size > file->size) {
        file->size   = new_size;
        node->length = new_size;
    }
    fat32_update_dir_entry(file->entry_dir_cluster, file->name, file->size, file->first_cluster);
    return (int32_t)written;
}

/* ============================================================
 * VFS Ops
 * ============================================================ */
static vfs_node_ops_t g_fat32_file_ops = {
    .read    = fat32_file_read,
    .write   = fat32_file_write,
    .open    = NULL,
    .close   = NULL,
    .readdir = NULL,
    .finddir = NULL,
};

static vfs_node_t* fat32_dir_finddir(vfs_node_t *node, const char *name);
static struct dirent* fat32_dir_readdir(vfs_node_t *node, uint32_t index);

static vfs_node_ops_t g_fat32_dir_ops = {
    .finddir = fat32_dir_finddir,
    .readdir = fat32_dir_readdir,
    .read    = NULL,
    .write   = NULL,
    .open    = NULL,
    .close   = NULL,
};

static struct dirent g_temp_dirent;

static vfs_node_t* fat32_create_vnode(fat32_dir_entry_t *entry, uint32_t parent_dir_cluster) {
    if (g_fat32_node_count >= FAT32_MAX_NODES) return NULL;
    char parsed_name[VFS_MAX_NAME_LEN];
    fat32_parse_83_name(entry->name, parsed_name);
    uint32_t start_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    for (uint32_t i = 0; i < g_fat32_node_count; i++) {
        if (g_fat32_vnodes[i].inode == start_cluster) return &g_fat32_vnodes[i];
    }
    uint32_t idx = g_fat32_node_count++;
    kstrncpy(g_fat32_files[idx].name, parsed_name, VFS_MAX_NAME_LEN);
    g_fat32_files[idx].first_cluster     = start_cluster;
    g_fat32_files[idx].size              = entry->file_size;
    g_fat32_files[idx].attr              = entry->attr;
    g_fat32_files[idx].entry_dir_cluster = parent_dir_cluster;
    g_fat32_files[idx].entry_index       = 0;
    kstrncpy(g_fat32_vnodes[idx].name, parsed_name, VFS_MAX_NAME_LEN);
    g_fat32_vnodes[idx].flags       = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    g_fat32_vnodes[idx].inode       = start_cluster;
    g_fat32_vnodes[idx].length      = entry->file_size;
    g_fat32_vnodes[idx].ops         = (entry->attr & FAT_ATTR_DIRECTORY) ? &g_fat32_dir_ops : &g_fat32_file_ops;
    g_fat32_vnodes[idx].device_data = &g_fat32_files[idx];
    return &g_fat32_vnodes[idx];
}

static vfs_node_t* fat32_dir_finddir(vfs_node_t *node, const char *name) {
    if (!node) return NULL;
    uint32_t dir_cluster = node->inode;
    dir_walk_ctx_t ctx;
    fat32_dir_entry_t entry;
    ctx.mode        = 1;
    ctx.target_name = name;
    ctx.out_entry   = &entry;
    ctx.write_entry = (void*)0;
    ctx.target_index = 0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    if (fat32_walk_dir(dir_cluster, &ctx)) {
        return fat32_create_vnode(&entry, dir_cluster);
    }
    return NULL;
}

static struct dirent* fat32_dir_readdir(vfs_node_t *node, uint32_t index) {
    if (!node) return NULL;
    uint32_t dir_cluster = node->inode;
    dir_walk_ctx_t ctx;
    fat32_dir_entry_t entry;
    ctx.mode         = 0;
    ctx.target_index = index;
    ctx.out_entry    = &entry;
    ctx.target_name  = (void*)0;
    ctx.write_entry  = (void*)0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    if (fat32_walk_dir(dir_cluster, &ctx)) {
        char parsed_name[VFS_MAX_NAME_LEN];
        fat32_parse_83_name(entry.name, parsed_name);
        uint32_t start_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
        kstrncpy(g_temp_dirent.name, parsed_name, VFS_MAX_NAME_LEN);
        g_temp_dirent.ino = start_cluster;
        return &g_temp_dirent;
    }
    return NULL;
}

/* ============================================================
 * Public Write API
 * ============================================================ */
uint32_t fat32_find_entry(uint32_t dir_cluster, const char *name) {
    dir_walk_ctx_t ctx;
    fat32_dir_entry_t entry;
    ctx.mode = 1;
    ctx.target_name = name;
    ctx.out_entry = &entry;
    ctx.write_entry = (void*)0;
    ctx.target_index = 0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    
    if (fat32_walk_dir(dir_cluster, &ctx)) {
        return ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    }
    return 0;
}

uint32_t fat32_create(uint32_t dir_cluster, const char *name) {
    dir_walk_ctx_t ctx;
    fat32_dir_entry_t existing;
    ctx.mode        = 1;
    ctx.target_name = name;
    ctx.out_entry   = &existing;
    ctx.write_entry = (void*)0;
    ctx.target_index = 0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    if (fat32_walk_dir(dir_cluster, &ctx)) {
        return ((uint32_t)existing.first_cluster_high << 16) | existing.first_cluster_low;
    }
    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) return 0;
    fat32_zero_cluster(new_cluster);
    fat32_dir_entry_t entry;
    for (int i = 0; i < 32; i++) ((uint8_t*)&entry)[i] = 0;
    fat32_name_to_83(name, entry.name);
    entry.attr               = FAT_ATTR_ARCHIVE;
    entry.first_cluster_high = (uint16_t)(new_cluster >> 16);
    entry.first_cluster_low  = (uint16_t)(new_cluster & 0xFFFF);
    entry.file_size          = 0;
    entry.creation_date      = (1 << 5) | 1;
    entry.write_date         = entry.creation_date;
    if (!fat32_write_dir_entry_to_dir(dir_cluster, &entry)) {
        fat32_write_fat_entry(new_cluster, FAT32_FREE);
        return 0;
    }
    serial_printf("[FAT32] Created file '%s' cluster=%u\n", name, new_cluster);
    return new_cluster;
}

uint32_t fat32_mkdir(uint32_t dir_cluster, const char *name) {
    dir_walk_ctx_t ctx;
    fat32_dir_entry_t existing;
    ctx.mode        = 1;
    ctx.target_name = name;
    ctx.out_entry   = &existing;
    ctx.write_entry = (void*)0;
    ctx.target_index = 0;
    ctx.result_lba = 0;
    ctx.result_byte_offset = 0;
    if (fat32_walk_dir(dir_cluster, &ctx)) {
        serial_printf("[FAT32] mkdir: '%s' already exists\n", name);
        return 0;
    }
    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) return 0;
    fat32_zero_cluster(new_cluster);

    /* Write '.' and '..' to first sector of new cluster */
    uint8_t sec[512];
    for (int i = 0; i < 512; i++) sec[i] = 0;

    fat32_dir_entry_t dot;
    for (int i = 0; i < 32; i++) ((uint8_t*)&dot)[i] = 0;
    dot.name[0] = '.';
    for (int i = 1; i < 11; i++) dot.name[i] = ' ';
    dot.attr               = FAT_ATTR_DIRECTORY;
    dot.first_cluster_high = (uint16_t)(new_cluster >> 16);
    dot.first_cluster_low  = (uint16_t)(new_cluster & 0xFFFF);
    dot.creation_date      = (1 << 5) | 1;
    dot.write_date         = dot.creation_date;

    fat32_dir_entry_t dotdot;
    for (int i = 0; i < 32; i++) ((uint8_t*)&dotdot)[i] = 0;
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    for (int i = 2; i < 11; i++) dotdot.name[i] = ' ';
    dotdot.attr               = FAT_ATTR_DIRECTORY;
    dotdot.first_cluster_high = (uint16_t)(dir_cluster >> 16);
    dotdot.first_cluster_low  = (uint16_t)(dir_cluster & 0xFFFF);
    dotdot.creation_date      = (1 << 5) | 1;
    dotdot.write_date         = dotdot.creation_date;

    for (int i = 0; i < 32; i++) sec[i]      = ((uint8_t*)&dot)[i];
    for (int i = 0; i < 32; i++) sec[32 + i] = ((uint8_t*)&dotdot)[i];
    uint32_t new_lba = fat32_cluster_to_lba(new_cluster);
    if (ata_write_sector(new_lba, sec) != 0) {
        fat32_write_fat_entry(new_cluster, FAT32_FREE);
        return 0;
    }

    fat32_dir_entry_t entry;
    for (int i = 0; i < 32; i++) ((uint8_t*)&entry)[i] = 0;
    fat32_name_to_83(name, entry.name);
    entry.attr               = FAT_ATTR_DIRECTORY;
    entry.first_cluster_high = (uint16_t)(new_cluster >> 16);
    entry.first_cluster_low  = (uint16_t)(new_cluster & 0xFFFF);
    entry.file_size          = 0;
    entry.creation_date      = (1 << 5) | 1;
    entry.write_date         = entry.creation_date;

    if (!fat32_write_dir_entry_to_dir(dir_cluster, &entry)) {
        fat32_write_fat_entry(new_cluster, FAT32_FREE);
        return 0;
    }
    serial_printf("[FAT32] Created directory '%s' cluster=%u\n", name, new_cluster);
    return new_cluster;
}

int fat32_unlink(uint32_t dir_cluster, const char *name) {
    dir_walk_ctx_t ctx_find;
    fat32_dir_entry_t entry;
    ctx_find.mode        = 1;
    ctx_find.target_name = name;
    ctx_find.out_entry   = &entry;
    ctx_find.write_entry = (void*)0;
    ctx_find.target_index = 0;
    ctx_find.result_lba = 0;
    ctx_find.result_byte_offset = 0;
    if (!fat32_walk_dir(dir_cluster, &ctx_find)) return -1;

    uint32_t first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;

    dir_walk_ctx_t ctx_del;
    fat32_dir_entry_t dummy;
    ctx_del.mode        = 3;
    ctx_del.target_name = name;
    ctx_del.out_entry   = &dummy;
    ctx_del.write_entry = (void*)0;
    ctx_del.target_index = 0;
    ctx_del.result_lba = 0;
    ctx_del.result_byte_offset = 0;
    fat32_walk_dir(dir_cluster, &ctx_del);

    if (first_cluster >= 2) fat32_free_chain(first_cluster);

    for (uint32_t i = 0; i < g_fat32_node_count; i++) {
        if (g_fat32_vnodes[i].inode == first_cluster) {
            g_fat32_vnodes[i].inode        = 0;
            g_fat32_files[i].first_cluster = 0;
        }
    }

    serial_printf("[FAT32] Deleted '%s'\n", name);
    return 0;
}

/* ============================================================
 * Root VFS Node
 * ============================================================ */
static vfs_node_t g_fat32_root_node = {
    .name        = "fat",
    .flags       = VFS_DIRECTORY,
    .inode       = 0,
    .length      = 0,
    .ops         = &g_fat32_dir_ops,
    .ptr         = NULL,
    .device_data = NULL
};

void fat32_init(void) {
    g_fat32_node_count = 0;
    uint8_t sector_buf[512];

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — REAL FAT32 Driver (Read/Write)\n");
    serial_printf("===========================================\n");

    if (ata_read_sector(FAT32_VOLUME_START_LBA, sector_buf) != 0) {
        serial_printf("[FAT32 FATAL] Failed to read FAT32 BPB!\n");
        return;
    }
    for (uint32_t i = 0; i < sizeof(fat32_bpb_t); i++)
        ((uint8_t*)&g_bpb)[i] = sector_buf[i];

    serial_printf("[FAT32 BPB] Bytes/Sector     : %u\n", g_bpb.bytes_per_sector);
    serial_printf("[FAT32 BPB] Sectors/Cluster  : %u\n", g_bpb.sectors_per_cluster);
    serial_printf("[FAT32 BPB] Root Dir Cluster : %u\n", g_bpb.root_cluster);

    g_first_fat_lba  = FAT32_VOLUME_START_LBA + g_bpb.reserved_sector_count;
    g_first_data_lba = g_first_fat_lba + (g_bpb.num_fats * g_bpb.fat_size_32);
    g_total_clusters = (g_bpb.total_sectors_32 - (g_first_data_lba - FAT32_VOLUME_START_LBA)) / g_bpb.sectors_per_cluster;

    g_fat32_root_node.inode = g_bpb.root_cluster;
    vfs_register_mount("/disk/fat0", &g_fat32_root_node);

    serial_printf("[FAT32] Mounted at /disk/fat0 (R/W). Total clusters: %u\n", g_total_clusters);
    serial_printf("===========================================\n\n");
}

void fat32_run_test_suite(void) {
    serial_printf("[FAT32 TEST] Reading WELCOME.TXT...\n");
    int fd = vfs_open("/disk/fat0/WELCOME.TXT", 0);
    if (fd >= 0) {
        char buf[512];
        for (int i = 0; i < 512; i++) buf[i] = 0;
        int32_t bytes = vfs_read(fd, buf, 511);
        vfs_close(fd);
        if (bytes > 0) {
            serial_printf("[FAT32 TEST PASSED] Read: %s\n\n", buf);
        }
    } else {
        serial_printf("[FAT32 TEST FAILED] Cannot open WELCOME.TXT\n");
    }
}

uint32_t fat32_get_root_cluster(void) {
    return g_bpb.root_cluster;
}
