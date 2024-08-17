bits 64

section .text

extern errno

global syscall0
global syscall1
global syscall2
global syscall3
global syscall4

set_errno_if_negative:
    cmp rdi, 0
    jge .end
    neg rdi
    mov [errno], rdi
    mov rax, -1
set_errno_if_negative.end:
    ret

syscall0:
    mov rax, rdi

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

syscall1:
    mov rax, rdi

    mov rdi, rsi

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

syscall2:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

syscall3:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

syscall4:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret
