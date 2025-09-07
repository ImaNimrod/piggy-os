.section .text

.extern syscall_handler

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    swapgs
    movq %rsp, %gs:24
    movq %gs:16, %rsp

    push $0x1b
    push %gs:24
    push %r11
    push $0x23
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

    mov %rsp, %rdi
    xor %rbp, %rbp
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

    addq $16, %rsp

    movq %gs:24, %rsp
    swapgs
    sysretq
.size syscall_entry, . - syscall_entry
