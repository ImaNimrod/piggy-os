#include <string.h>

int strncmp(const char* str1, const char* str2, size_t n) {
    for (; n; str1++, str2++, n--) {
        int delta = *str1 - *str2;
        if (delta) {
            return delta;
        }

        if (*str1 == '\0') {
            break;
        }
    }

    return 0;
}
