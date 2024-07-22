#include <stdlib.h>
#include <string.h>

void* calloc(size_t nmemb, size_t size) {
    size_t length;

    if (__builtin_mul_overflow(nmemb, size, &length)) {
        return NULL;
    }

    void* ptr = malloc(length);
    if (ptr) {
        memset(ptr, 0, length);
    }
    return ptr;
}
