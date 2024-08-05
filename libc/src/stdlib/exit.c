#include <unistd.h>
#include "stdlib_internal.h"

__attribute__((noreturn)) void exit(int status) {
    __call_atexit_handlers();
    _exit(status);
}
