bits 64

extern main

section .text
    
global _start

_start:
    call main

    mov rdi, rax
    mov rax, 1
    syscall

    jmp $
