#include <assert.h>
#include <stdlib.h>

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    assert("malloc not implemented");
    return NULL;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }

    assert("free not implemented");
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return malloc(new_size);
    }

    assert("realloc not implemented");
    return NULL;
}
