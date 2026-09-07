; =============================================================================
; ZeruX OS — Low-Level Assembly Syscall Gate (int 0x80)
; File: kernel/arch/syscall_asm.asm
; =============================================================================
;
; Ring 3 (User Mode) uygulamaları `int 0x80` fırlattığında IDT Gate 0x80 (128)
; bu stub'a zıplar.
;
; registers_t layout (isr.h) şu sırayla stack'te:
;   gs, fs, es, ds  (4 push)   ← registers_t başlangıcı (en düşük adres)
;   edi,esi,ebp,esp,ebx,edx,ecx,eax  (pusha)
;   int_no, err_code  (bizim push'larımız)
;   eip, cs, eflags   (CPU auto)
;   useresp, ss       (CPU auto, ring3 geçişte)
;
; DİKKAT: registers_t içindeki alan sırası (isr.h):
;   ds, edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err_code, eip, cs, eflags, useresp, ss
; Bu yapı SADECE 1 segment alanı (ds) içerdiğinden, burada da yalnızca ds push ediyoruz.
; =============================================================================

[BITS 32]

global syscall_stub
extern syscall_handler_c

syscall_stub:
    push dword 0                ; Dummy error code
    push dword 0x80             ; Interrupt vector 128 (0x80)

    pusha                       ; Push EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX

    ; Yalnızca DS'i push et (registers_t struct'ında tek bir 'ds' alanı var)
    push ds

    ; Kernel Data Segment (0x10) yükle
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; `registers_t *regs` parametresini stack'e koy
    call syscall_handler_c      ; C Syscall Handler'ı çağır
    add esp, 4                  ; Parametreyi temizle

    ; Dönen değeri (EAX) sakla — popa EAX'i ezer, önce koy
    mov [esp + 32], eax         ; pusha'daki EAX slotunu güncelle (offset: ds=0, edi=4..eax=32)

    pop ds                      ; DS'i geri yükle (ES/FS/GS zaten kernel data)
    ; ES, FS, GS'i de user data selector'a geri döndür
    push eax
    mov ax, 0x23
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    popa                        ; General register'ları geri yükle
    add esp, 8                  ; Dummy error code ve int_no temizle
    iret                        ; Ring 3 User Mode'a dön!
