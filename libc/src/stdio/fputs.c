#include <string.h>
#include "stdio_internal.h"

int fputs(const char* s, FILE* stream) {
    size_t len = strlen(s);
    if (fwrite(s, sizeof(char), len, stream) != len) {
        return EOF;
    }

    return 0;
}
