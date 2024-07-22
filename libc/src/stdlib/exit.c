#include <stdlib.h>
#include <unistd.h>

extern void _call_atexit_handlers(void);
extern void _fini(void);

__attribute__((noreturn)) void exit(int status) {
    _call_atexit_handlers();
    _fini();
    exit(status);
}
