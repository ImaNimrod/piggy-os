#include <limits.h>
#include <sys/types.h>
#include "stdlib_internal.h"

int atexit(void (*handler)(void)) {
    for (size_t i = 0; i < ATEXIT_MAX; i++) {
        if (atexit_handlers[i] == NULL) {
            atexit_handlers[i] = handler;
            return 0;
        }
    }

    return -1;
}
