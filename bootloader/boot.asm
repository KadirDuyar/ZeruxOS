; =============================================================================
; ZeruX OS — Stage 1 Bootloader (boot.asm)
; =============================================================================
;
; This file is the Master Boot Record (MBR). The BIOS loads it to physical
; address 0x7C00 and hands control here. We are in 16-bit REAL MODE.
;
; Execution flow:
;   1.  Initialize CPU registers & stack (real mode)
;   2.  Load Stage 2 Bootloader from disk → temporary buffer at 0x8000
;   3.  Jump to Stage 2 (0x8000)
;
; Assembled with:  nasm -f bin boot.asm -o boot.bin
; =============================================================================

[BITS 16]       ; Assemble as 16-bit code (real mode)
[ORG  0x7C00]   ; Tell NASM: our code is loaded at physical address 0x7C00

; ─── Compile-time constants ──────────────────────────────────────────────────
STAGE2_SECTORS  equ 4          ; Number of 512-byte sectors to read for Stage 2
STAGE2_SEG      equ 0x0800     ; 0x0800 * 16 = 0x8000

; =============================================================================
; STAGE 1 — Real Mode Initialization
; =============================================================================
start:
    cli                         ; Disable hardware interrupts while we set up
    xor ax, ax                  ; AX = 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; Stack Pointer at 0x7C00
    sti                         ; Re-enable interrupts

    mov [boot_drive], dl        ; Save boot drive

; =============================================================================
; Load Stage 2 from Disk (Sectors 2..5)
; =============================================================================
load_stage2:
    mov ax, STAGE2_SEG
    mov es, ax                  ; ES = 0x0800 → read buffer base at physical 0x8000
    xor bx, bx                  ; ES:BX = 0x0800:0000 = physical 0x8000

    mov ah, 0x02                ; BIOS Read Sectors
    mov al, STAGE2_SECTORS      ; Number of sectors
    mov ch, 0x00                ; Cylinder 0
    mov cl, 0x02                ; Sector 2 (Sector 1 is MBR)
    mov dh, 0x00                ; Head 0
    mov dl, [boot_drive]        ; Drive number
    int 0x13
    jc disk_error               ; If carry flag is set, read failed

    ; Jump to Stage 2
    jmp 0x0000:0x8000

disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
.halt:
    hlt
    jmp .halt

boot_drive db 0

; =============================================================================
; BOOT SIGNATURE & PARTITION TABLE
; =============================================================================
times 446 - ($ - $$) db 0      ; Zero-pad code to byte 446

; ── Partition 1: FAT32 Volume ────────────────────────────────────────────────
db 0x00                 ; Boot indicator (0x00 = non-bootable, 0x80 = bootable)
db 0xFF, 0xFF, 0xFF     ; Starting CHS (dummy values for LBA only)
db 0x0C                 ; OS Type (0x0C = FAT32 LBA)
db 0xFF, 0xFF, 0xFF     ; Ending CHS (dummy values for LBA only)
dd 256                  ; Starting LBA (Sector 256)
dd 300                  ; Sector count (Size of FAT32 volume)

; ── Partition 2 (Empty) ──────────────────────────────────────────────────────
times 16 db 0
; ── Partition 3 (Empty) ──────────────────────────────────────────────────────
times 16 db 0
; ── Partition 4 (Empty) ──────────────────────────────────────────────────────
times 16 db 0

dw 0xAA55               ; Boot sector signature
