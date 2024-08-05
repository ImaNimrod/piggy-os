#include <string.h>
#include "stdlib_internal.h"

void* calloc(size_t nmemb, size_t size) {
    size_t length = nmemb * size;

    void* ptr = malloc(length);
    if (ptr) {
        memset(ptr, 0, length);
    }
    return ptr;
}
