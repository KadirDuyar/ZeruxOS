/* =============================================================================
 * ZeruX OS — ELF32 Parser and Loader Implementation
 * File: kernel/task/elf.c
 * =============================================================================
 */

#include "elf.h"
#include "serial.h"

/* =============================================================================
 * elf32_check_header() — Validate ELF Header
 * =============================================================================
 * Returns: 1 if valid, 0 if invalid
 */
int elf32_check_header(const elf32_ehdr_t *ehdr) {
    if (!ehdr) return 0;

    /* Check Magic (0x7F 'E' 'L' 'F') */
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        serial_printf("[ELF] Error: Invalid Magic Number\n");
        return 0;
    }

    /* Check Class (32-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        serial_printf("[ELF] Error: Not a 32-bit ELF\n");
        return 0;
    }

    /* Check Endianness (Little Endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        serial_printf("[ELF] Error: Not Little Endian\n");
        return 0;
    }

    /* Check Machine (x86) */
    if (ehdr->e_machine != EM_386) {
        serial_printf("[ELF] Error: Not compiled for x86 (EM_386)\n");
        return 0;
    }

    /* Check Type (Executable) */
    if (ehdr->e_type != ET_EXEC) {
        serial_printf("[ELF] Error: Not an Executable (ET_EXEC)\n");
        return 0;
    }

    return 1;
}

/* =============================================================================
 * elf32_dump_header() — Print ELF Header Details
 * =============================================================================
 */
void elf32_dump_header(const elf32_ehdr_t *ehdr) {
    if (!ehdr) return;

    serial_printf("=== ELF32 Header Dump ===\n");
    serial_printf("  Magic    : 0x%x\n", *(uint32_t*)ehdr->e_ident);
    serial_printf("  Type     : %d (2=EXEC)\n", ehdr->e_type);
    serial_printf("  Machine  : %d (3=386)\n", ehdr->e_machine);
    serial_printf("  Entry Pt : 0x%x\n", ehdr->e_entry);
    serial_printf("  PH Off   : %u bytes\n", ehdr->e_phoff);
    serial_printf("  PH Num   : %u\n", ehdr->e_phnum);
    serial_printf("  PH Size  : %u bytes\n", ehdr->e_phentsize);
    serial_printf("=========================\n");
}

/* =============================================================================
 * elf32_load_and_exec() — Load ELF from VFS and Create Ring 3 Process
 * =============================================================================
 */
#include "vfs.h"
#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "task.h"

int elf32_load_and_exec(const char *path, int argc, const char **argv) {
    serial_printf("[ELF] Attempting to load '%s'\n", path);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        serial_printf("[ELF] Failed to open '%s'\n", path);
        return -1;
    }

    /* GCC page-aligns segments, so even a small C program can have a 12KB+ ELF file.
     * We allocate 64KB to ensure we read the entire file. */
    uint32_t file_size = 65536; 
    uint8_t *file_buf = kmalloc(file_size);
    if (!file_buf) {
        serial_printf("[ELF] Out of memory allocating file buffer\n");
        vfs_close(fd);
        return -1;
    }

    int bytes_read = vfs_read(fd, file_buf, file_size);
    vfs_close(fd);

    if (bytes_read == (int)file_size) {
        serial_printf("[ELF] WARNING: File size reached 64KB limit. It might be truncated!\n");
    }

    if (bytes_read < (int)sizeof(elf32_ehdr_t)) {
        serial_printf("[ELF] File too small to be an ELF\n");
        kfree(file_buf);
        return -1;
    }

    elf32_ehdr_t *ehdr = (elf32_ehdr_t*)file_buf;

    if (!elf32_check_header(ehdr)) {
        serial_printf("[ELF] Invalid ELF format in '%s'\n", path);
        kfree(file_buf);
        return -1;
    }

    /* Adım 1.5: e_phnum Sınır Kontrolü (Güvenlik) - Integer Overflow Korumalı */
    if ((uint64_t)ehdr->e_phoff + (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize > (uint64_t)bytes_read) {
        serial_printf("[ELF] REJECTED: Program Headers exceed file size in '%s'\n", path);
        kfree(file_buf);
        return -1;
    }

    /* Adım 2: Süreç İzolasyonu - Yeni Page Directory oluştur */
    page_directory_t *pdir = vmm_create_user_dir();
    if (!pdir) {
        serial_printf("[ELF] Failed to create Page Directory\n");
        kfree(file_buf);
        return -1;
    }

    elf32_phdr_t *phdr = (elf32_phdr_t*)(file_buf + ehdr->e_phoff);

    uint32_t max_vaddr = 0;

    serial_printf("=== ELF32 Program Headers ===\n");
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint32_t memsz = phdr[i].p_memsz;
            uint32_t filesz = phdr[i].p_filesz;
            uint32_t vaddr = phdr[i].p_vaddr;
            uint32_t offset = phdr[i].p_offset;

            /* Adım 2.5: Güvenlik vaddr Doğrulaması (Privilege Escalation Koruması) */
            /* User Space = 0x08048000. Altındaki her şey (0x0 - 32MB arası) Kernel paylaşımlı alandır. */
            if (vaddr < 0x08048000 || (uint64_t)vaddr + memsz > 0xC0000000) {
                serial_printf("[ELF] REJECTED: p_vaddr (0x%x) points to kernel/invalid memory!\n", vaddr);
                pmm_free_block(pdir); /* Sızıntıyı önle */
                kfree(file_buf);
                return -1;
            }

            if (vaddr + memsz > max_vaddr) {
                max_vaddr = vaddr + memsz;
            }

            serial_printf("  [PT_LOAD] Offset: 0x%x -> VAddr: 0x%x (MemSz: %u bytes)\n", offset, vaddr, memsz);

            /* Allocate and Map Pages for this segment */
            for (uint32_t j = 0; j < memsz; j += 4096) {
                void *phys = pmm_alloc_block();
                if (!phys) {
                    serial_printf("[ELF] Out of physical memory!\n");
                    pmm_free_block(pdir);
                    kfree(file_buf);
                    return -1;
                }

                vmm_map_page(pdir, vaddr + j, (uint32_t)phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);

                /* Initialize page to 0 (BSS) */
                uint8_t *dest = (uint8_t*)phys;
                for (int k = 0; k < 4096; k++) dest[k] = 0;

                /* Copy file data into the physical page */
                if (j < filesz) {
                    uint32_t to_copy = (filesz - j > 4096) ? 4096 : (filesz - j);
                    uint8_t *src = file_buf + offset + j;
                    for (uint32_t k = 0; k < to_copy; k++) dest[k] = src[k];
                }
            }
        }
    }
    serial_printf("=============================\n");

    /* Create the task using the new Page Directory */
    task_t *new_task = user_process_create((void (*)(void))ehdr->e_entry, "ELF App", pdir, argc, argv);
    if (!new_task) {
        serial_printf("[ELF] Failed to create user process\n");
        kfree(file_buf);
        return -1;
    } else {
        /* Initialize User Heap (sbrk) */
        uint32_t heap_start = (max_vaddr + 0xFFF) & ~0xFFF; /* Align to 4KB */
        new_task->heap_start = heap_start;
        new_task->heap_end = heap_start;

        serial_printf("[ELF] Application loaded and scheduled. PID: %u\n", new_task->pid);
    }

    kfree(file_buf);
    return new_task->pid;
}
