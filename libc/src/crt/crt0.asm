bits 64

section .text

global _start

extern environ

extern __init_stdio_buffers
extern exit
extern main

_start:
    mov [environ], rdx

    call __init_stdio_buffers
    call main

    mov rdi, rax
    call exit

    jmp $
