#ifndef _KERNEL_UTILS_HASHMAP_H
#define _KERNEL_UTILS_HASHMAP_H 1

#include <stdbool.h>
#include <stddef.h>

typedef struct hashmap hashmap_t;

hashmap_t* hashmap_create(size_t size);
void hashmap_destroy(hashmap_t* hm);
bool hashmap_get(hashmap_t* hm, const void* key, size_t key_size, void** value);
bool hashmap_set(hashmap_t* hm, const void* key, size_t key_size, void* value);
bool hashmap_remove(hashmap_t* hm, const void* key, size_t key_size);
size_t hashmap_size(hashmap_t* hm);

#endif /* _KERNEL_UTILS_HASHMAP_H */
