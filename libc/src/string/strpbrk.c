#include <string.h>

char* strpbrk(const char* str1, const char* str2) {
    while (*str1 != '\0') {
        for (size_t i = 0; str2[i]; i++) {
            if (*str1 == str2[i]) {
                return (char*) str1;
            }
        }

        str1++;
    }

    return NULL;
}
