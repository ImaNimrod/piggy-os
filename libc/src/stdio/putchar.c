#include "stdio_internal.h"

int putchar(int c) {
    return fputc(c, stdout);
}
