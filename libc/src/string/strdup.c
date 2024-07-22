#include <stdlib.h>
#include <string.h>

char* strdup(const char* str) {
    size_t len = strlen(str) + 1;

    char* dup = malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}
