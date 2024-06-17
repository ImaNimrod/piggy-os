bits 64

extern main

section .text

global syscall0
global syscall1
global _start

syscall0:
    mov rax, rdi
    syscall
    ret

syscall1:
    mov rax, rdi
    mov rdi, rsi
    syscall
    ret

_start:
    call main

    mov rax, 2
    syscall

    jmp $
