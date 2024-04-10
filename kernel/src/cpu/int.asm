extern isr_handler

%macro pushall 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rsi
    push rdi
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro popall 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rdi
    pop rsi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

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
    pushall

    mov rdi, rsp
	xor rbp, rbp
    cld
    call isr_handler

    popall

    add rsp, 8
    iretq
