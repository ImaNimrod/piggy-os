#include <string.h>

char* strchr(const char* str, int c) {
    do {
        if (*str == (char) c) {
            return (char*) str;
        }
    } while (*str++);

    return NULL;
}
