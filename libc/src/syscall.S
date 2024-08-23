.intel_syntax noprefix

.section .text

.extern errno

.type set_errno_if_negative, @function
set_errno_if_negative:
    cmp rdi, 0
    jge .end
    neg rdi
    mov [errno], rdi
    mov rax, -1
.end:
    ret

.global syscall0
.type syscall0, @function
syscall0:
    mov rax, rdi

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

.global syscall1
.type syscall1, @function
syscall1:
    mov rax, rdi

    mov rdi, rsi

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

.global syscall2
.type syscall2, @function
syscall2:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

.global syscall3
.type syscall3, @function
syscall3:
    mov rax, rdi

    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx

    syscall

    mov rdi, rax
    call set_errno_if_negative
    ret

.global syscall4
.type syscall4, @function
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
