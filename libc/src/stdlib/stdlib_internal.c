#include <limits.h>
#include <sys/types.h>
#include "stdlib_internal.h"

char** __heap_environ;
size_t __heap_environ_length;
size_t __heap_environ_size;

void (*atexit_handlers[ATEXIT_MAX])(void);

void __call_atexit_handlers(void) {
    for (ssize_t i = ATEXIT_MAX - 1; i >= 0; i--) {
        if (atexit_handlers[i]) {
            atexit_handlers[i]();
        }
    }
}
