#include <string.h>

char* strncpy(char* __restrict dest, const char* __restrict src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];

        if (!src[i]) {
            break;
        }
    }

    return dest;
}
