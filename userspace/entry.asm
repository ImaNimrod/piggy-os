bits 64

section .text

global _start
global syscall0

syscall0:
    mov rax, rdi
    syscall
    ret

_start:
    extern main
    call main
.loop:
    jmp .loop
