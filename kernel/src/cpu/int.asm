bits 64

section .text

extern isr_handler

isr_common_format:
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
	cld
	mov rdi, rsp
	xor rbp, rbp
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
	add rsp, qword 16
	iretq

%macro isr 1

global _isr%1
_isr%1:
	push qword 0
	push qword %1
	jmp isr_common_format

%endmacro

%macro error_isr 1

global _isr%1
_isr%1:
	push qword %1
	jmp isr_common_format

%endmacro

%define has_errcode(i) (i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30)

%assign i 0
%rep 256
%if !has_errcode(i)
	isr i
%else
	error_isr i
%endif
%assign i i + 1
%endrep
