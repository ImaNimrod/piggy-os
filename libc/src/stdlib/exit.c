#include <stdlib.h>
#include <unistd.h>

extern void __call_atexit_handlers(void);

__attribute__((noreturn)) void exit(int status) {
    __call_atexit_handlers();
    _exit(status);
}
