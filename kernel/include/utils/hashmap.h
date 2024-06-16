#ifndef _KERNEL_UTILS_HASHMAP_H
#define _KERNEL_UTILS_HASHMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct hashmap_entry {
	char* key;
	void* value;
	struct hashmap_entry* next;
} hashmap_entry_t;

typedef struct hashmap {
	size_t size;
	hashmap_entry_t** entries;
} hashmap_t;

hashmap_t* hashmap_create(size_t entry_count);
void hashmap_destroy(hashmap_t* map);
bool hashmap_set(hashmap_t* map, const void* key, size_t key_size, void* value);
void* hashmap_get(hashmap_t* map, const void* key, size_t key_size);
bool hashmap_remove(hashmap_t* map, const void* key, size_t key_size);

#endif /* _KERNEL_UTILS_HASHMAP_H */
