[BITS 16]
[ORG 0x8000]

; ─── Compile-time constants ──────────────────────────────────────────────────
KERNEL_LBA      equ 5                   ; Kernel starts at sector 6 (LBA 5)
KERNEL_SECTORS  equ 500                 ; 500 sectors = 250 KB
KERNEL_LOAD_SEG equ 0x1000              ; Physical 0x10000
KERNEL_FINAL    equ 0x100000            ; Kernel final address (1 MB)

; Segment Selectors for GDT
CODE_SEG        equ gdt_code - gdt_start
DATA_SEG        equ gdt_data - gdt_start

start:
    ; DL contains boot drive from Stage 1
    mov [boot_drive], dl

    ; Set segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Clear boot_info_t struct (32 bytes at 0x7E00)
    mov di, 0x7E00
    mov cx, 16      ; 16 words = 32 bytes
    cld
    rep stosw

    ; Print greeting
    mov si, msg_stage2
    call print_string

    ; Detect Memory (E820)
    mov si, msg_e820
    call print_string
    call detect_memory_e820

    ; Load Kernel via CHS
    mov si, msg_load
    call print_string
    call load_kernel

    ; Setup VESA
    call setup_vesa

    ; Enable A20 Line (Fast A20)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Switch to Protected Mode
    mov si, msg_pmode
    call print_string_serial ; Print to serial because screen is VESA now!

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:init_pm

; -----------------------------------------------------------------------------
; 16-bit Subroutines
; -----------------------------------------------------------------------------
print_string_serial:
.loop_ser:
    lodsb
    cmp al, 0
    je .done_ser
    mov dx, 0x3F8
    out dx, al
    jmp .loop_ser
.done_ser:
    ret

print_string:
.loop:
    lodsb
    cmp al, 0
    je .done
    mov ah, 0x0E
    int 0x10
    jmp .loop
.done:
    ret

; =============================================================================
; STAGE 1.7 — VESA Setup
; =============================================================================
setup_vesa:
    xor ax, ax
    mov es, ax

    mov ax, 0x4F00
    mov di, 0x9200
    mov byte [di], 'V'
    mov byte [di+1], 'B'
    mov byte [di+2], 'E'
    mov byte [di+3], '2'
    int 0x10
    cmp ax, 0x004F
    jne .vesa_failed
    
    ; Target 1: 1024x768x32
    mov word [target_w], 1024
    mov word [target_h], 768
    mov byte [target_bpp], 32
    call find_mode
    jnc .found

    ; Target 2: 1024x768x24
    mov byte [target_bpp], 24
    call find_mode
    jnc .found

    ; Target 3: 800x600x32
    mov word [target_w], 800
    mov word [target_h], 600
    mov byte [target_bpp], 32
    call find_mode
    jnc .found

    ; Target 4: 800x600x24
    mov byte [target_bpp], 24
    call find_mode
    jnc .found

    jmp .vesa_failed

.found:
    ; Populate boot_info_t (0x7E00)
    movzx eax, word [vbe_mode]
    mov [0x7E0C], eax
    mov eax, [0x9428]
    mov [0x7E10], eax
    xor eax, eax
    mov ax, [0x9410]
    mov [0x7E14], eax
    mov ax, [0x9412]
    mov [0x7E18], eax
    mov ax, [0x9414]
    mov [0x7E1C], eax
    xor eax, eax
    mov al, [0x9419]
    mov [0x7E20], eax

    ; Set Mode
    mov ax, 0x4F02
    mov bx, [vbe_mode]
    or bx, 0x4000       ; Enable LFB
    int 0x10

    mov si, msg_vesa_ok
    call print_string_serial
    ret

.vesa_failed:
    mov si, msg_vesa_fail
    call print_string_serial
    ret

target_w dw 0
target_h dw 0
target_bpp db 0

find_mode:
    mov ax, ds
    push ax
    lds si, [0x920E] ; Load VideoModePtr to DS:SI

.mode_loop:
    mov cx, [si]
    cmp cx, 0xFFFF
    je .mode_not_found
    add si, 2

    pusha
    mov ax, 0x4F01
    mov di, 0x9400
    push cs
    pop es
    int 0x10
    cmp ax, 0x004F
    jne .next_mode_pop

    mov ax, [es:di + 0x00]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .next_mode_pop

    ; Check against targets
    mov ax, [cs:target_w]
    cmp word [es:di + 0x12], ax
    jne .next_mode_pop
    mov ax, [cs:target_h]
    cmp word [es:di + 0x14], ax
    jne .next_mode_pop
    mov al, [cs:target_bpp]
    cmp byte [es:di + 0x19], al
    jne .next_mode_pop

    ; Match Found!
    popa
    pop ax
    mov ds, ax
    mov [vbe_mode], cx

    ; Get info again for ES:DI = 0:0x9400
    mov ax, 0x4F01
    mov di, 0x9400
    int 0x10
    
    clc ; Clear carry = success
    ret

.next_mode_pop:
    popa
    jmp .mode_loop

.mode_not_found:
    pop ax
    mov ds, ax
    stc ; Set carry = fail
    ret


; =============================================================================
; STAGE 1.5 — BIOS E820 Memory Detection
; =============================================================================
detect_memory_e820:
    mov di, 0x8000              ; ES:DI = 0x0000:0x8000 (temp buffer for array, wait, our code is at 0x8000!)
    ; Oh no, our code is at 0x8000. Let's put the E820 map at 0x9000!
    mov di, 0x9000
    xor ebx, ebx                ; Continuation marker (start at 0)
    xor ebp, ebp                ; EBP = entry count
    mov edx, 0x534D4150         ; EDX = 'SMAP' (0x534D4150)
    mov eax, 0x0000E820         ; EAX = 0xE820
    mov dword [es:di + 20], 1   ; Default ACPI 3.0 extended attribute = 1
    mov ecx, 24                 ; Request 24 bytes
    int 0x15
    jc .e820_failed             ; Carry flag set on first call = unsupported
    cmp eax, 0x534D4150         ; Must return 'SMAP' in EAX
    jne .e820_failed

.e820_loop:
    inc ebp                     ; Valid entry -> count++
    cmp ebx, 0                  ; EBX == 0 means end of list
    je .e820_done
    add di, 24                  ; Advance destination buffer by 24 bytes
    mov eax, 0x0000E820         ; Reset EAX = 0xE820
    mov dword [es:di + 20], 1   ; Default ACPI 3.0 attribute = 1
    mov ecx, 24                 ; Request 24 bytes
    mov edx, 0x534D4150         ; Reset EDX = 'SMAP'
    int 0x15
    jnc .e820_loop              ; Carry clear -> process next entry

.e820_done:
    mov dword [0x7E00], 0x5A455255 ; Boot info magic "ZERU"
    mov [0x7E04], ebp              ; Entry count
    mov dword [0x7E08], 0x9000     ; E820 map pointer (Updated to 0x9000)
    ret

.e820_failed:
    mov dword [0x7E00], 0x00000000 ; Magic = 0 (Failed)
    mov dword [0x7E04], 0
    mov dword [0x7E08], 0
    ret

; =============================================================================
; Load Kernel via CHS (INT 13h AH=02h)
; =============================================================================
load_kernel:
    ; 1. Get Drive Geometry
    mov ah, 0x08
    mov dl, [boot_drive]
    push es
    int 0x13
    pop es
    jc disk_error
    
    and cl, 0x3F
    mov [SectorsPerTrack], cl
    inc dh
    mov [Heads], dh

    ; 2. Initialize Read
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx
    
    mov di, KERNEL_SECTORS
    
    ; Kernel starts at LBA 5 -> Cylinder 0, Head 0, Sector 6
    mov ch, 0   ; Cylinder
    mov dh, 0   ; Head
    mov cl, 6   ; Sector
    mov dl, [boot_drive]

.read_loop:
    cmp di, 0
    je .done

    ; Read 1 sector
    mov ah, 0x02
    mov al, 1
    pusha
    int 0x13
    jc disk_error_pop
    popa

    dec di
    
    ; Advance memory buffer
    mov ax, es
    add ax, 0x20  ; 512 bytes = 0x20 segments
    mov es, ax

    ; Advance CHS
    inc cl
    cmp cl, [SectorsPerTrack]
    jbe .read_loop
    
    mov cl, 1
    inc dh
    cmp dh, [Heads]
    jb .read_loop
    
    mov dh, 0
    inc ch
    jmp .read_loop

.done:
    ret

disk_error_pop:
    popa
    ; fall-through to disk_error

disk_error:
    mov si, msg_disk_error
    call print_string
.halt:
    hlt
    jmp .halt

SectorsPerTrack db 0
Heads db 0

; -----------------------------------------------------------------------------
; Data Section
; -----------------------------------------------------------------------------
boot_drive      db 0
msg_stage2      db "Stage2 Loaded.", 13, 10, 0
msg_e820        db "E820 OK.", 13, 10, 0
msg_load        db "Loading kernel...", 13, 10, 0
msg_pmode       db "Switching to PMode...", 13, 10, 0
msg_disk_error  db "Disk error!", 13, 10, 0
msg_vesa_ok     db "VESA OK.", 13, 10, 0
msg_vesa_fail   db "VESA Failed! Booting VGA Text.", 13, 10, 0
vbe_mode        dw 0

; Disk Address Packet (DAP) for INT 13h AH=42h
dap:
    dap_size      db 0x10
    dap_zero      db 0
    dap_count     dw 0
    dap_offset    dw 0
    dap_segment   dw 0
    dap_lba_lower dd 0
    dap_lba_upper dd 0

; -----------------------------------------------------------------------------
; Global Descriptor Table (GDT)
; -----------------------------------------------------------------------------
align 16
gdt_start:
    ; Null Descriptor (0x00)
    dd 0x00000000
    dd 0x00000000

gdt_code:
    ; Code Segment (0x08) - Base: 0, Limit: 4GB
    dw 0xFFFF       ; Limit (bits 0-15)
    dw 0x0000       ; Base (bits 0-15)
    db 0x00         ; Base (bits 16-23)
    db 10011010b    ; Access byte (Present, Ring 0, Executable, Readable)
    db 11001111b    ; Flags (32-bit, 4KB granularity) & Limit (bits 16-19)
    db 0x00         ; Base (bits 24-31)

gdt_data:
    ; Data Segment (0x10) - Base: 0, Limit: 4GB
    dw 0xFFFF       ; Limit
    dw 0x0000       ; Base
    db 0x00         ; Base
    db 10010010b    ; Access byte (Present, Ring 0, Writable)
    db 11001111b    ; Flags & Limit
    db 0x00         ; Base

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Limit (Size of GDT)
    dd gdt_start                ; Base Address of GDT

; =============================================================================
; 32-bit Protected Mode Entry Point
; =============================================================================
[BITS 32]
init_pm:
    ; Initialize segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up a new stack
    mov esp, 0x90000
    mov ebp, esp

    ; Move Kernel from physical 0x10000 to final address 0x100000
    mov esi, KERNEL_LOAD_SEG * 16       ; Source: 0x10000
    mov edi, KERNEL_FINAL               ; Destination: 0x100000
    mov ecx, (KERNEL_SECTORS * 512) / 4 ; Count in DWORDS
    cld
    rep movsd

    ; Jump to Kernel
    push dword 0x7E00                   ; Argument 1: boot_info_t pointer
    push dword 0x00000000               ; Dummy return address
    jmp CODE_SEG:KERNEL_FINAL
