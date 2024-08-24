#include <errno.h>
#include <string.h>
#include "stdlib_internal.h"

__attribute__((malloc)) void* calloc(size_t nmemb, size_t size) {
    size_t total_size;

    if (__builtin_mul_overflow(nmemb, size, &total_size)) {
        errno = EOVERFLOW;
        return NULL;
    } 

    void* ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}
