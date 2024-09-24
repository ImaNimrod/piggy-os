.global setjmp
.type setjmp, @function
setjmp:
    mov %rbx, 0(%rdi)
    mov %rbp, 8(%rdi)
    mov %rsp, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r13, 32(%rdi)
    mov %r14, 40(%rdi)
    mov %r15, 48(%rdi)
    mov (%rsp), %rax
    mov %rax, 56(%rdi)
    mov $0, %rax
    ret
.size setjmp, . - setjmp

.global longjmp
.type longjmp, @function
longjmp:
    mov 0(%rdi), %rbx
    mov 8(%rdi), %rbp
    mov 16(%rdi), %rsp
    mov 24(%rdi), %r12
    mov 32(%rdi), %r13
    mov 40(%rdi), %r14
    mov 48(%rdi), %r15
    mov 56(%rdi), %rax
    mov %rax, (%rsp)

    mov %rsi, %rax
    test %rax, %rax
    jnz 1f
    inc %rax
1:  ret
.size longjmp, . - longjmp
