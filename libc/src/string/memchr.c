#include <string.h>

void* memchr(const void* s, int c, size_t size) {
    const char* p = s;

    for (size_t i = 0; i < size; i++) {
        if (p[i] == (char) c) {
            return (void*) &p[i];
        }
    }

    return NULL;
}
