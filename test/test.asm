bits 64

global _begin
_begin:
mov rax, 0
syscall
call loop

loop:
jmp loop
