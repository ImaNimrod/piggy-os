#include "stdio_internal.h"

int puts(const char* s) {
    return fputs(s, stdout);
}
