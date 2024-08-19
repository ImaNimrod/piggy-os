#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned long jmp_buf[8];

__attribute__((noreturn)) void longjmp(jmp_buf, int);
int setjmp(jmp_buf) __attribute__((__returns_twice__));

#endif /* _SETJMP_H */
