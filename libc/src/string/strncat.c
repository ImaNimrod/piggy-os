#include <string.h>

char* strncat(char* __restrict dest, const char* __restrict src, size_t n) {
    size_t d = strlen(dest);
    size_t i = 0;

    for (i = 0; i < n && src[i]; i++) {
        dest[d + i] = src[i];
    }

    dest[d + i] = '\0';
    return dest;
}
