bits 64

section .text

global _start
_start:
    mov rax, 4
    syscall
    mov rcx, rax

    mov rax, 0
    syscall

.loop:
    jmp .loop
