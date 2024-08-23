.section .text

.extern environ

.extern __init_stdio_buffers
.extern _init
.extern exit
.extern main

.global _start
.type _start, @function
_start:
    and $-16, %rsp

    mov %rdx, [environ]

    push %rdi
    push %rsi
    call __init_stdio_buffers
    pop %rsi
    pop %rdi

    call main

    mov %rax, %rdi
    call exit

.loop:
    jmp .loop
.size _start, . - _start
