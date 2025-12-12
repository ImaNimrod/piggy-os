.section .text

.extern syscall_handler

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    swapgs

    // there aren't any syscalls (FOR NOW) that use all six arguments
    // we use r9 to switch stacks because SYSV ABI says it is safe to clobber and in the meantime,
    movq %gs:8, %r9
    movq 0(%r9), %r9
    xchg %r9, %rsp

    push $0x1b
    push %r9
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

    addq $56, %rsp

    mov %r9, %rsp
    swapgs
    sysretq
.size syscall_entry, . - syscall_entry
