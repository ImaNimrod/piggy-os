bits 64

section .init
global _init
_init:
    push rbp
    mov rsp, rbp
    ; gcc will nicely put the contents of crtbegin.o's .init section here.

section .fini
global _fini
_fini:
    push rbp
    mov rsp, rbp
    ; gcc will nicely put the contents of crtbegin.o's .fini section here.
