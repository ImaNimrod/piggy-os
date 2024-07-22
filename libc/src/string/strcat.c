#include <string.h>

char* strcat(char* __restrict dest, const char* __restrict src) {
    size_t len = strlen(dest);

    for (size_t i = 0;; i++) {
        dest[len + i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }

    return dest;
}
