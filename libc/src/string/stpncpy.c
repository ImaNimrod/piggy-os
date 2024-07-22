#include <string.h>

char* stpncpy(char* __restrict str1, const char* __restrict str2, size_t n) {
    size_t i;
    for (i = 0; i < n && str2[i] != '\0'; i++) {
        str1[i] = str2[i];
    }

    char* result = &str1[i];
    for (; i < n; i++) {
        str1[i] = '\0';
    }

    return result;
}
