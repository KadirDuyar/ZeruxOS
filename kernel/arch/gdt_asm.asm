; =============================================================================
; ZeruX OS — GDT & TSS Low-Level Assembly Primitives
; File: kernel/arch/gdt_asm.asm
; =============================================================================
;
; LGDT instruction ile dinamik GDT'yi yükler, CS/DS/ES/FS/GS/SS segment
; yazmaçlarını yeniden doldurur ve LTR instruction ile TSS selector'ını (0x28)
; Task Register'a kaydeder.
; =============================================================================

[BITS 32]

global gdt_flush
global tss_flush

; =============================================================================
; gdt_flush(gdt_ptr_t *gdt_ptr)
; =============================================================================
; Parametre: [ESP+4] = Pointer to gdt_ptr_t struct
;
gdt_flush:
    mov eax, [esp + 4]          ; EAX = gdt_ptr adresi
    lgdt [eax]                  ; Load Global Descriptor Table Register (GDTR)

    ; Data selector'ları (0x10) tüm segment yazmaçlarına yükle
    mov ax, 0x10                ; 0x10 = Kernel Data Segment Descriptor (GDT index 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump ile Code Segment (0x08) yazmacını (CS) tazele
    jmp 0x08:.reload_cs

.reload_cs:
    ret                         ; Dönüş yap

; =============================================================================
; tss_flush()
; =============================================================================
; Load Task Register (LTR) komutu ile GDT index 5 (0x28) TSS descriptor'ını yükler.
;
tss_flush:
    mov ax, 0x28                ; 0x28 = TSS Descriptor Selector (GDT index 5)
    ltr ax                      ; Load Task Register (TR)
    ret
