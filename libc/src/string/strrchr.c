#include <string.h>

char* strrchr(const char* str, int c) {
    size_t len = strlen(str);
    if (len == 0) {
        return NULL;
    }

    while (len--) {
        if (str[len] == (char) c) {
            return (char*) &str[len];
        }
    }

    return NULL;
}
