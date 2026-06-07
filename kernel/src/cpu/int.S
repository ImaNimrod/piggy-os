.section .text

.extern isr_handler

.macro interrupt_stub i
.global _isr\i
.type _isr\i, @function
_isr\i:
    .if !(\i == 8 || (\i >= 10 && \i <= 14) || \i == 17 || \i == 21 || \i == 29 || \i == 30)
        pushq $0
    .endif
    pushq $\i
    jmp isr_entry
.endm

.altmacro

.set i, 0
.rept 256
    interrupt_stub %i
    .set i, i+1
.endr

.type isr_entry, @function
isr_entry:
    cmpq $0x08, 24(%rsp)
    je 1f
    swapgs
    1:

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

    cld
    mov %rsp, %rdi
    xor %rbp, %rbp
    call isr_handler

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

    cmpq $0x08, 8(%rsp)
    je 2f
    swapgs
    2:
    iretq
.size isr_entry, . - isr_entry
