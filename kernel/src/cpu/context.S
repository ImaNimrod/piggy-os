.section .text

.global context_switch
.type context_switch, @function
context_switch:
    cli
    mov %rdi, %rsp
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
.size context_switch, . - context_switch

.global context_call_and_switch
.type context_call_and_switch, @function
context_call_and_switch:
    mov 0(%rsp), %r11
    mov %rsp, %r10
    addq $8, %r10

    pushq $0x10
    push %r10
    pushf
    pushq $0x08
    push %r11

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

    mov %rdi, %r11
    mov %rsp, %rdi
    call *%r11

    cli
    mov %rsp, %rdi
    call context_switch
.size context_call_and_switch, . - context_call_and_switch
