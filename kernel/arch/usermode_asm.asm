; =============================================================================
; ZeruX OS — Low-Level Assembly User Mode (Ring 3) Switch
; File: kernel/arch/usermode_asm.asm
; =============================================================================
;
; `enter_usermode_asm(entry_point, user_stack_top)`
;   - DS, ES, FS, GS segment yazmaçlarına User Data Selector (0x23) yükler.
;   - Donanımsal `iret` komutu için stack üzerine [SS, ESP, EFLAGS, CS, EIP]
;     çerçevesini dizerek işlemciyi Ring 0'dan Ring 3 (User Mode)'e düşürür.
; =============================================================================

[BITS 32]

global enter_usermode_asm

enter_usermode_asm:
    ; Parametreler:
    ; [ESP + 4] = entry_point (User program giriş adresi)
    ; [ESP + 8] = user_stack_top (User mode stack tepesi)

    mov ebx, [esp + 4]          ; EBX = entry_point
    mov ecx, [esp + 8]          ; ECX = user_stack_top

    ; 1. Data segment yazmaçlarını User Data Selector (0x23 -> Index 4 | RPL 3) ile yükle
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 2. Donanımsal `iret` Stack Çerçevesi Oluştur
    ; Stack Düzeni (Yukarıdan Aşağıya):
    ;   [SS_user]   = 0x23 (User Data Segment Selector)
    ;   [ESP_user]  = ECX  (User Stack Pointer)
    ;   [EFLAGS]    = 0x202 (Bit 9 = IF / Interrupts Enabled)
    ;   [CS_user]   = 0x1B (User Code Segment Selector 0x18 | RPL 3)
    ;   [EIP_user]  = EBX  (User Entry Point)

    push dword 0x23             ; SS (User Data)
    push ecx                    ; ESP (User Stack)
    push dword 0x202            ; EFLAGS (IF=1)
    push dword 0x1B             ; CS (User Code)
    push ebx                    ; EIP (User Entry)

    ; 3. Donanımsal IRET fırlatarak Ring 3'e Düş!
    iret
