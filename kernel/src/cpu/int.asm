extern isr_handler

%macro interrupt_stub 1
    %%start:
    %if !(i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30)
        push qword 0
    %endif
    push qword %1
    jmp interrupt_handler
    times 32 - ($ - %%start) db 0
%endmacro

global idt_stubs
idt_stubs:
    %assign i 0
    %rep 256
        interrupt_stub i
    %assign i i +  1
    %endrep

interrupt_handler:
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

    mov rdi, rsp
	xor rbp, rbp
    cld
    call isr_handler

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

    add rsp, 16
    iretq
