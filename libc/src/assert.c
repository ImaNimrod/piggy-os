#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void __assert_internal(const char* file, int line, const char* func, const char* condition) {
    fprintf(stderr, "Assertion failed in %s:%d (%s): %s\n", file, line, func, condition);
    exit(EXIT_FAILURE);
}
