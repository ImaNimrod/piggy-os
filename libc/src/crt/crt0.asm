bits 64

section .text

global _start

extern environ

extern __init_stdio_buffers
extern _init
extern exit
extern main

_start:
    and rsp, -16

    mov [environ], rdx

    push rdi
    push rsi
    call __init_stdio_buffers
    pop rsi
    pop rdi

    call main

    mov rdi, rax
    call exit

    jmp $
