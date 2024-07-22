#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEAP_CHUNK_MAGIC 0xffba67ed89ab32be

struct heap_chunk {
    size_t size: 63;
    size_t free: 1;
    unsigned long magic;
    struct heap_chunk* next;
};

static struct heap_chunk *first, *last;

static struct heap_chunk* get_free_chunk(size_t size) {
    struct heap_chunk* iter = first;

    while (iter) {
        if (iter->free && iter->size >= size && iter->magic == HEAP_CHUNK_MAGIC) {
            return iter;
        }
        iter = iter->next;
    }

    return NULL;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    struct heap_chunk* chunk = get_free_chunk(size);
    if (chunk) {
        chunk->free = 0;
        return (void*) (chunk + 1);
    }

    size_t total_size = sizeof(struct heap_chunk) + size;

    chunk = sbrk(total_size);
    if (chunk == (void*) -1) {
        return NULL;
    }

	chunk->size = size;
	chunk->free = 0;
    chunk->magic = HEAP_CHUNK_MAGIC;
	chunk->next = NULL;

	if (!first) {
		first = chunk;
    }
	if (last) {
		last->next = chunk;
    }
	last = chunk;

    return (void*) (chunk + 1);
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }

	struct heap_chunk* chunk = (struct heap_chunk*) ptr - 1;
    assert(chunk->magic == HEAP_CHUNK_MAGIC && "invalid heap pointer");

    if ((uintptr_t) ptr + chunk->size == (uintptr_t) sbrk(0)) {
        if (first == last) {
            first = NULL;
            last = NULL;
        } else {
            struct heap_chunk* iter = first;
            while (iter) {
                if (iter->next == last) {
                    iter->next = NULL;
                    last = iter;
                    break;
                }

                iter = iter->next;
            }
        }

        sbrk(-(sizeof(struct heap_chunk) - chunk->size));
        return;
    }

    chunk->free = 1;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return malloc(new_size);
    }

    if (new_size == 0) {
        return NULL;
    }

    struct heap_chunk* chunk = (struct heap_chunk*) ptr - 1;
    if (chunk->size >= new_size) {
        return ptr;
    }

    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, chunk->size);
        free(ptr);
    }

    return new_ptr;
}
