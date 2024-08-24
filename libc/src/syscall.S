.section .text

.extern errno

.type set_errno_if_negative, @function
set_errno_if_negative:
    cmp $0, %rdi
    jge .end
    neg %rdi
    mov %rdi, errno
    mov $-1, %rax
.end:
    ret
.size set_errno_if_negative, . - set_errno_if_negative

.global syscall0
.type syscall0, @function
syscall0:
    mov %rdi, %rax

    syscall

    mov %rax, %rdi
    call set_errno_if_negative
    ret
.size syscall0, . - syscall0

.global syscall1
.type syscall1, @function
syscall1:
    mov %rdi, %rax

    mov %rsi, %rdi

    syscall

    mov %rax, %rdi
    call set_errno_if_negative
    ret
.size syscall1, . - syscall1

.global syscall2
.type syscall2, @function
syscall2:
    mov %rdi, %rax

    mov %rsi, %rdi
    mov %rdx, %rsi

    syscall

    mov %rax, %rdi
    call set_errno_if_negative
    ret
.size syscall2, . - syscall2

.global syscall3
.type syscall3, @function
syscall3:
    mov %rdi, %rax

    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx

    syscall

    mov %rax, %rdi
    call set_errno_if_negative
    ret
.size syscall3, . - syscall3

.global syscall4
.type syscall4, @function
syscall4:
    mov %rdi, %rax

    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10

    syscall

    mov %rax, %rdi
    call set_errno_if_negative
    ret
.size syscall4, . - syscall4
