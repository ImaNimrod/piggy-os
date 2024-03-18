#ifndef _KERNEL_SLAB_H
#define _KERNEL_SLAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct slab;

struct cache {
    const char *name;

    size_t object_size;
    size_t pages_per_slab;

    void (*ctor)(void*);
    void (*dtor)(void*);

    struct slab* empty_slabs;
    struct slab* partial_slabs;
    struct slab* full_slabs;

    struct cache* next;
};

struct cache* cache_create(const char* name, size_t object_size, void (*ctor)(void*), void (*dtor)(void*));
void cache_destroy(struct cache* cache);

void* cache_alloc_object(struct cache* cache);
bool cache_free_object(struct cache* cache, void* object);

void slab_init(void);

void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t size);

#endif /* _KERNEL_SLAB_H */
