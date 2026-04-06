.section .text

.extern syscall_handler

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    swapgs

    movq %rax, %gs:32

    movq %gs:8, %rax
    movq 0(%rax), %rax
    xchg %rax, %rsp

    pushq $0x1b
    push %rax
    movq %gs:32, %rax
    push %r11
    pushq $0x23
    push %rcx

    subq $16, %rsp
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rbp
    push %rdi
    push %rsi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    sti

    xor %rbp, %rbp
    mov %rsp, %rdi
    call syscall_handler

    cli

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rsi
    pop %rdi
    pop %rbp
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    addq $40, %rsp

    pop %rsp
    swapgs
    sysretq
.size syscall_entry, . - syscall_entry
