#include <string.h>

int strcmp(const char* str1, const char* str2) {
    for (size_t i = 0;; i++) {
        char c1 = str1[i];
        char c2 = str2[i];

        if (c1 != c2) {
            return c1 - c2;
        }

        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}
