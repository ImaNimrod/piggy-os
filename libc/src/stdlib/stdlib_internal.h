#ifndef STDLIB_INTERNAL_H
#define STDLIB_INTERNAL_H

#include <limits.h>
#include <stdlib.h>

extern char** __heap_environ;
extern size_t __heap_environ_size;
extern size_t __heap_environ_length;

extern void (*atexit_handlers[ATEXIT_MAX])(void);

void __call_atexit_handlers(void);

#endif /* STDLIB_INTERNAL_H */
