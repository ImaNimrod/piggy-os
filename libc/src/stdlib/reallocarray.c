#include <errno.h>
#include "stdlib_internal.h"

__attribute__((malloc)) void* reallocarray(void* ptr, size_t size1, size_t size2) {
    size_t total_size;

    if (__builtin_mul_overflow(size1, size2, &total_size)) {
        errno = EOVERFLOW;
        return NULL;
    }

    return realloc(ptr, total_size);
}
