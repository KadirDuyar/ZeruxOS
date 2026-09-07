; =============================================================================
; ZeruX OS — Low-Level Assembly IRQ Stubs
; File: kernel/arch/irq_asm.asm
; =============================================================================
;
; Bu dosya donanım kesmeleri (IRQ 0..15) için düşük seviyeli assembly giriş
; noktalarını (stubs) tanımlar.
;
; IRQ'lar IDT üzerinde Vektör 32..47 (0x20..0x2F) arasına yerleştirilmiştir.
; =============================================================================

[bits 32]

; C tarafındaki ana IRQ dispatcher fonksiyonu
extern irq_handler

; =============================================================================
; NASM MACRO TANIMI — IRQ Stub Oluşturucu
; =============================================================================
; %1: IRQ Numarası (0..15)
; %2: Karşılık gelen IDT Vektör Numarası (32..47)
%macro IRQ_STUB 2
global irq_stub_%1
irq_stub_%1:
    push dword 0    ; Dummy error code (stack uyumluluğu için)
    push dword %2   ; IDT Vektör Numarası (32 + IRQ)
    jmp irq_common_stub
%endmacro

; =============================================================================
; IRQ 0..15 STUB TANIMLARI
; =============================================================================
IRQ_STUB 0,  32   ; IRQ 0  -> Timer (PIT)
IRQ_STUB 1,  33   ; IRQ 1  -> Keyboard
IRQ_STUB 2,  34   ; IRQ 2  -> Cascade Slave PIC
IRQ_STUB 3,  35   ; IRQ 3  -> Serial COM2
IRQ_STUB 4,  36   ; IRQ 4  -> Serial COM1
IRQ_STUB 5,  37   ; IRQ 5  -> LPT2 / Sound
IRQ_STUB 6,  38   ; IRQ 6  -> Floppy
IRQ_STUB 7,  39   ; IRQ 7  -> LPT1
IRQ_STUB 8,  40   ; IRQ 8  -> RTC Clock
IRQ_STUB 9,  41   ; IRQ 9  -> ACPI / PCI
IRQ_STUB 10, 42   ; IRQ 10 -> PCI
IRQ_STUB 11, 43   ; IRQ 11 -> PCI
IRQ_STUB 12, 44   ; IRQ 12 -> PS/2 Mouse
IRQ_STUB 13, 45   ; IRQ 13 -> FPU
IRQ_STUB 14, 46   ; IRQ 14 -> Primary ATA Hard Disk
IRQ_STUB 15, 47   ; IRQ 15 -> Secondary ATA Hard Disk

; =============================================================================
; irq_common_stub — Tüm IRQ'ların Ortak Bağlantı Noktası
; =============================================================================
global irq_common_stub
irq_common_stub:
    ; 1. Tüm Genel Amaçlı Register'ları Sakla (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    pusha

    ; 2. Mevcut Data Segment Selector'ı Sakla
    mov ax, ds
    push eax

    ; 3. Kernel Data Segment'ini (0x10) Yükle
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 4. C Handler'ını Çağır: registers_t* pointer'ını argüman olarak geç
    push esp
    call irq_handler
    add esp, 4      ; C argümanını temizle

    ; 5. Orijinal Data Segment'ini Geri Yükle
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 6. Genel Amaçlı Register'ları Geri Yükle
    popa

    ; 7. Stack'ten Vector Number ve Dummy Error Code'u Temizle (2 * 4 = 8 byte)
    add esp, 8

    ; 8. Interrupt Dönüşü
    iretd
