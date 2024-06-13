#ifndef _KERNEL_MEM_SLAB_H
#define _KERNEL_MEM_SLAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

struct slab;

struct cache {
    const char* name;

    size_t object_size;
    size_t pages_per_slab;

    struct slab* empty_slabs;
    struct slab* partial_slabs;
    struct slab* full_slabs;

    spinlock_t lock;

    struct cache* next;
};

void* cache_alloc_object(struct cache* cache);
bool cache_free_object(struct cache* cache, void* object);
struct cache* slab_cache_create(const char* name, size_t object_size);
void slab_init(void);

// TODO: kmalloc implementation using slab allocator
void* kmalloc(size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);

#endif /* _KERNEL_MEM_SLAB_H */
