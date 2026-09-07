; =============================================================================
; ZeruX OS — Low-Level Assembly Context Switch Primitives
; File: kernel/task/switch_asm.asm
; =============================================================================
;
; `switch_to(task_t *old, task_t *next)`
;   - Mevcut görevin (old) CPU register'larını (EFLAGS + PUSHAD) stack'e kaydeder.
;   - ESP yazmacını `old->esp` (offset 48) içine yazar.
;   - Yeni görevin (next) `next->esp` değerini ESP yazmacına yükler.
;   - POPAD ve POPFD ile yeni görevin register'larını geri yükler.
;   - `ret` komutu ile yeni görevin EIP adresine sıçrar.
; =============================================================================

[BITS 32]

global switch_to

switch_to:
    ; 1. Mevcut Görevin EFLAGS ve Genel Yazmaçlarını Stack'e Kaydet
    pushfd                      ; EFLAGS kaydet (4 bytes)
    pushad                      ; EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX kaydet (32 bytes)

    ; Toplam stack kayması = 36 bytes
    ; [ESP + 40] = old task_t pointer
    ; [ESP + 44] = next task_t pointer

    mov eax, [esp + 40]         ; EAX = old task pointer
    mov edx, [esp + 44]         ; EDX = next task pointer

    ; 2. Mevcut ESP'yi old->esp (task_t içinde offset 52) alanına yaz
    mov [eax + 52], esp

    ; 3. Yeni ESP'yi next->esp (task_t içinde offset 52) alanından yükle
    mov esp, [edx + 52]

    ; 4. Yeni Görevin Yazmaçlarını ve EFLAGS Değerini Geri Yükle
    popad                       ; 32 bytes pop et
    popfd                       ; EFLAGS (4 bytes) pop et (EFLAGS.IF = 1 yapılır)

    ; 5. Yeni Görevin EIP Adresine Zıpla!
    ret                         ; Return to next task's EIP
