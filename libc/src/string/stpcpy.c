#include <string.h>

char* stpcpy(char* __restrict str1, const char* __restrict str2) {
    while (*str2) {
        *str1++ = *str2++;
    }

    *str1 = '\0';
    return str1;
}
