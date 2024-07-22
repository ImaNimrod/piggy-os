#include <string.h>

size_t strcspn(const char* str, const char* characters) {
    size_t result = 0;

    while (str[result] != '\0') {
        for (size_t i = 0; characters[i]; i++) {
            if (str[result] == characters[i]) {
                return result;
            }
        }

        result++;
    }

    return result;
}
