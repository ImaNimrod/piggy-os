#include <string.h>

char* strcpy(char* __restrict dest, const char* __restrict src) {
    char* result = dest;

    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';
    return result;
}
