#include <string.h>

size_t strspn(const char* str1, const char* str2) {
    size_t len = 0;

    while (*str1 != '\0') {
        if (!strchr(str2, *str1)) {
            return len;
        }

        len++;
        str1++;
    }

    return len;
}
