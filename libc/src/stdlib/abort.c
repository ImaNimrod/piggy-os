#include "stdlib_internal.h"

__attribute__((noreturn)) void abort(void) {
    exit(EXIT_FAILURE);
}
