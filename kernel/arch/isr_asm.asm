; =============================================================================
; ZeruX OS — Low-Level Assembly ISR Stubs
; File: kernel/arch/isr_asm.asm
; =============================================================================
;
; Bu dosya 0–31 arası tüm CPU exception'ları için düşük seviyeli assembly
; giriş noktalarını (stubs) tanımlar.
;
; HATA KODU (ERROR CODE) KURALLARI:
;   x86 CPU bazı exception'lar için stack'e otomatik olarak 32-bit Hata Kodu
;   (Error Code) push eder, bazıları için ETMEZ.
;
;   Hata Kodu PUSH EDENException'lar:
;     8 (#DF), 10 (#TS), 11 (#NP), 12 (#SS), 13 (#GP), 14 (#PF), 17 (#AC)
;
;   Diğer tüm exception'lar için biz kendimiz dummy `0` push ederiz.
;   Böylece C tarafındaki `registers_t` yapısı her exception için TAM OLARAK
;   aynı bellek düzenine (layout) sahip olur.
; =============================================================================

[bits 32]

; C tarafındaki ana dispatcher fonksiyonu
extern isr_handler

; =============================================================================
; NASM MACRO TANIMLARI
; =============================================================================

; Error Code üretmeyen Exception'lar için makro (Dummy 0 push eder)
%macro ISR_NOERRCODE 1
global isr_stub_%1
isr_stub_%1:
    push dword 0    ; Dummy error code
    push dword %1   ; Exception vektör numarası
    jmp isr_common_stub
%endmacro

; Error Code üreten Exception'lar için makro (CPU zaten error code push etti)
%macro ISR_ERRCODE 1
global isr_stub_%1
isr_stub_%1:
    push dword %1   ; Exception vektör numarası
    jmp isr_common_stub
%endmacro

; =============================================================================
; 0–31 CPU EXCEPTION STUB'LARI
; =============================================================================

ISR_NOERRCODE 0   ; #DE Divide-by-Zero
ISR_NOERRCODE 1   ; #DB Debug
ISR_NOERRCODE 2   ; NMI Non-Maskable Interrupt
ISR_NOERRCODE 3   ; #BP Breakpoint (int3)
ISR_NOERRCODE 4   ; #OF Overflow
ISR_NOERRCODE 5   ; #BR BOUND Range Exceeded
ISR_NOERRCODE 6   ; #UD Invalid Opcode (ud2)
ISR_NOERRCODE 7   ; #NM Device Not Available
ISR_ERRCODE   8   ; #DF Double Fault (Error code pushed)
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun
ISR_ERRCODE   10  ; #TS Invalid TSS (Error code pushed)
ISR_ERRCODE   11  ; #NP Segment Not Present (Error code pushed)
ISR_ERRCODE   12  ; #SS Stack-Segment Fault (Error code pushed)
ISR_ERRCODE   13  ; #GP General Protection Fault (Error code pushed)
ISR_ERRCODE   14  ; #PF Page Fault (Error code pushed)
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; #MF x87 Floating-Point Exception
ISR_ERRCODE   17  ; #AC Alignment Check (Error code pushed)
ISR_NOERRCODE 18  ; #MC Machine Check
ISR_NOERRCODE 19  ; #XM SIMD Floating-Point Exception
ISR_NOERRCODE 20  ; #VE Virtualization Exception
ISR_NOERRCODE 21  ; Reserved
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Reserved
ISR_NOERRCODE 29  ; Reserved
ISR_NOERRCODE 30  ; Reserved
ISR_NOERRCODE 31  ; Reserved

; =============================================================================
; isr_common_stub — Tüm ISR'lerin Ortak Bağlantı Noktası
; =============================================================================
;
; Stack Durumu (isr_common_stub'a girildiğinde):
;   [esp + 0] : int_no (vektör numarası)
;   [esp + 4] : err_code (donanım veya dummy 0)
;   [esp + 8] : eip
;   [esp + 12]: cs
;   [esp + 16]: eflags
; =============================================================================

global isr_common_stub
isr_common_stub:
    ; 1. Tüm Genel Amaçlı Register'ları Sakla (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    pusha

    ; 2. Mevcut Data Segment Selector'ı Sakla
    mov ax, ds
    push eax

    ; 3. Kernel Data Segment'ini (0x10) Tüm Segment Register'larına Yükle
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 4. C Handler'ını Çağır: argüman olarak stack pointer'ı (registers_t*) geç
    push esp
    call isr_handler
    add esp, 4      ; C argümanını temizle

    ; 5. Orijinal Data Segment'ini Geri Yükle
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 6. Genel Amaçlı Register'ları Geri Yükle
    popa

    ; 7. Stack'ten Vector Number ve Error Code'u Temizle (2 * 4 = 8 byte)
    add esp, 8

    ; 8. Interrupt Dönüşü: EIP, CS, EFLAGS, (ve varsa Ring 3 ESP, SS) Geri Yüklenir
    iretd
