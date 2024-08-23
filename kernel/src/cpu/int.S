.section .text

.extern isr_handler

.macro interrupt_stub num
.global _isr\num
.type _isr\num, @function
_isr\num:
    .if !(\num == 8 || (\num >= 10 && \num <= 14) || \num == 17 || \num == 21 || \num == 29 || \num == 30)
        pushq $0
    .endif
    pushq $\num
    jmp interrupt_entry
.endm

.altmacro

.set i, 0
.rept 256
    interrupt_stub %i
    .set i, i+1
.endr

.type interrupt_entry, @function
interrupt_entry:
    cmpq $0xff, 0(%rsp)
    je panic_ipi_handler

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

    iretq
.size interrupt_entry, . - interrupt_entry

// this is run by each of the CPUs when they receive a panic IPI
.type panic_ipi_handler, @function
panic_ipi_handler:
    cli
.loop:
    hlt
    jmp .loop
.size panic_ipi_handler, . - panic_ipi_handler
