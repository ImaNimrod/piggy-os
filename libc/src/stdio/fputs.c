#include "stdio_internal.h"

int fputs(const char* s, FILE* stream) {
    while (*s != '\0') {
        if (fputc(*s++, stream) < 0) {
            return EOF;
        }
    }

    return 1;
}
