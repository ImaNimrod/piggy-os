bits 64

extern main

section .text
    
global _start

_start:
    call main

    mov rdi, rax
    mov rax, 0
    syscall

    jmp $
