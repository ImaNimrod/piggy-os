#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>

static void (*atexit_handlers[ATEXIT_MAX])(void);

void _call_atexit_handlers(void) {
    for (ssize_t i = ATEXIT_MAX - 1; i >= 0; i--) {
        if (atexit_handlers[i]) {
            atexit_handlers[i]();
        }
    }
}

int atexit(void (*handler)(void)) {
    for (size_t i = 0; i < ATEXIT_MAX; i++) {
        if (atexit_handlers[i] == NULL) {
            atexit_handlers[i] = handler;
            return 0;
        }
    }

    return -1;
}
