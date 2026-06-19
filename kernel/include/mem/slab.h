#ifndef _KERNEL_MEM_SLAB_H
#define _KERNEL_MEM_SLAB_H

#include <stddef.h>

struct slab_cache;

struct slab_cache* slab_cache_create(const char* name, size_t object_size);
void slab_cache_destroy(struct slab_cache* cache);
void* slab_cache_alloc(struct slab_cache* cache);
bool slab_cache_free(struct slab_cache* cache, void* object);

void slab_init(void);

__attribute__((malloc)) void* kmalloc(size_t size);
__attribute__((malloc)) void* kmallocz(size_t size);
__attribute__((malloc)) void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);

#endif /* _KERNEL_MEM_SLAB_H */
