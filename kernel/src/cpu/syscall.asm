bits 64

section .text

extern syscall_handler

global syscall_entry
syscall_entry:
    swapgs
    mov qword [gs:0024], rsp
    mov rsp, qword [gs:0016]

    push 0x1b               ; ss
    push qword [gs:0024]    ; rsp
    push r11                ; rflags
    push 0x23               ; cs
    push rcx                ; rip

    sub rsp, qword 16 

    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    sti

    mov rdi, rsp
    xor rbp, rbp
    call syscall_handler

    cli

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, qword 16

    mov rsp, qword [gs:0024]
    swapgs

    o64 sysret
