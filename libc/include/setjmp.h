#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned long jmp_buf[8];

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int _setjmp(jmp_buf) __attribute__((__returns_twice__));
__attribute__((noreturn)) void _longjmp (jmp_buf, int);
#endif

__attribute__((noreturn)) void longjmp(jmp_buf, int);
int setjmp(jmp_buf) __attribute__((__returns_twice__));

#endif /* _SETJMP_H */
