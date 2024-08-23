.intel_syntax noprefix

.section .text

.global setjmp
.type setjmp, @function
setjmp:
    mov [rdi + 0], rbx
    mov [rdi + 8], rbp
    mov [rdi + 16], rsp
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    mov rax, [rsp]
    mov [rdi + 56], rax
    xor rax, rax
    ret

.global longjmp
.type longjmp, @function
longjmp:
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov rsp, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    mov rax, [rdi + 56]
    mov [rsp], rax

    mov rax, rsi
    test rax, rax
    jnz longjmp.end
    inc rax
.end:
    ret
