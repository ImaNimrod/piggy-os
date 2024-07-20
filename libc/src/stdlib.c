#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t seed = 1;

int abs(int x) {
    return x >= 0 ? x : -x;
}

int rand(void) {
    seed = seed * 1103515245 + 12345;
    return (unsigned int) (seed / 65536) % 32768;
}

void srand(unsigned int s) {
    seed = s;
}

#define CHUNK_MAGIC 0xfaab45873dce509a

struct chunk {
    size_t size: 63;
    size_t free: 1;
    unsigned long magic;
    void* data;
    struct chunk *next, *prev;
};

static struct chunk* base = NULL;

static inline size_t word_align(size_t size) {
    return (size + (sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1);
}

static void* base_chunk(void) {
    if (base == NULL) {
        base = sbrk(word_align(sizeof(struct chunk)));
        if (base == (void*) -1) {
            _exit(-1);
        }

        base->size = 0;
        base->free = 0;
        base->magic = CHUNK_MAGIC;
        base->next = NULL;
        base->prev = NULL;
    }

    return base;
}

struct chunk* chunk_find(size_t size, struct chunk** heap) {
    struct chunk* c = base_chunk();
    for (; c && (!c->free || c->size < size); *heap = c, c = c->next);
    return c;
}

static void chunk_merge_next(struct chunk* c) {
    c->size = c->size + c->next->size + sizeof(struct chunk);
    c->next = c->next->next;
    if (c->next) {
        c->next->prev = c;
    }
}

static void chunk_split_next(struct chunk* c, size_t size) {
    struct chunk* next = (struct chunk*) ((uintptr_t) c + size);
    next->prev = c;
    next->next = c->next;
    next->size = c->size - size;
    next->free = 1;

    if (c->next) {
        c->next->prev = next;
    }

    c->next = next;
    c->size = size - sizeof(struct chunk);
}

void* malloc(size_t size) {
    if (!size) {
        return NULL;
    }

    size_t actual_size = word_align(size + sizeof(struct chunk));

    struct chunk* prev = NULL;
    struct chunk* c = chunk_find(size, &prev);
    if (!c) {
        struct chunk* new = sbrk(actual_size);
        if (new == (void*) -1) {
            return NULL;
        }

        new->next = NULL;
        new->prev = prev;
        new->magic = CHUNK_MAGIC;
        new->size = actual_size - sizeof(struct chunk);
        prev->next = new;
        c = new;
    } else if (actual_size + sizeof(size_t) < c->size) {
        chunk_split_next(c, actual_size);
    }

    c->free = 0;
    return (void*) (c + 1);
}

void free(void* ptr) {
    if (!ptr || (uintptr_t) ptr < (uintptr_t) base_chunk() || (uintptr_t) ptr > (uintptr_t) sbrk(0)) {
        return;
    }

    struct chunk* c = (struct chunk*) ptr - 1;
    if (c->magic != CHUNK_MAGIC) {
        return;
    }

    c->free = 1;

    if (c->next && c->next->free) {
        chunk_merge_next(c);
    }

    if (c->prev->free) {
        c = c->prev;
        chunk_merge_next(c);
    }

    if (!c->next) {
        c->prev->next = NULL;
        sbrk(- c->size - sizeof(struct chunk));
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t length = nmemb * size;
    void* ptr = malloc(length);
    if (ptr) {
        memset(ptr, 0, length);
    }
    return ptr;
}
