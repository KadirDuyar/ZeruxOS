[BITS 32]

global _start
extern main
extern exit

section .text
_start:
    ; Kernel sys_spawn puts argc and argv on the top of the stack.
    ; So ESP points to argc, and ESP+4 points to argv.
    
    pop eax      ; EAX = argc
    pop ebx      ; EBX = argv
    
    ; Push arguments for main(argc, argv)
    push ebx     ; push argv
    push eax     ; push argc
    
    call main
    
    ; Push return value of main as status for exit
    push eax
    call exit
    
.halt:
    jmp .halt
