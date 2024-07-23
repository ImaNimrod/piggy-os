#include "stdio_internal.h"

int fgetc(FILE* stream) {
    unsigned char c;

    if (fread(&c, 1, 1, stream) != 1) {
        return EOF;
    }

    return c;
}

extern int getc(FILE* stream) __attribute__((weak, alias("fgetc")));
