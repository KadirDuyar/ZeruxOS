/* =============================================================================
 * ZeruX OS — ELF32 Executable Format Definitions
 * File: kernel/include/elf.h
 * =============================================================================
 */

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define ELF_MAGIC 0x464C457F /* 0x7F 'E' 'L' 'F' in Little Endian */

/* e_ident[] indices */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9
#define EI_NIDENT    16

/* e_ident[] values */
#define ELFCLASSNONE  0
#define ELFCLASS32    1
#define ELFCLASS64    2

#define ELFDATANONE   0
#define ELFDATA2LSB   1  /* Little Endian */
#define ELFDATA2MSB   2  /* Big Endian */

#define EV_CURRENT    1

/* e_type values */
#define ET_NONE       0
#define ET_REL        1
#define ET_EXEC       2
#define ET_DYN        3
#define ET_CORE       4

/* e_machine values */
#define EM_NONE       0
#define EM_386        3  /* Intel 80386 */

/* ELF 32-bit Header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

/* Program Header Types */
#define PT_NULL       0
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_NOTE       4
#define PT_SHLIB      5
#define PT_PHDR       6

/* Program Header Flags */
#define PF_X          1  /* Executable */
#define PF_W          2  /* Writable */
#define PF_R          4  /* Readable */

/* ELF 32-bit Program Header (Phdr) */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

/* Function Prototypes */
int elf32_check_header(const elf32_ehdr_t *ehdr);
void elf32_dump_header(const elf32_ehdr_t *ehdr);
int elf32_load_and_exec(const char *path, int argc, const char **argv);

#endif /* ELF_H */
