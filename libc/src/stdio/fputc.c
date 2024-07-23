#include "stdio_internal.h"

int fputc(int c, FILE* stream) {
    if (fwrite(&c, 1, 1, stream) != 1) {
        return EOF;
    }

    return c;
}

extern int putc(int c, FILE* stream) __attribute__((weak, alias("fputc")));
