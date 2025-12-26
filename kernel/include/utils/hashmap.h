#ifndef _KERNEL_UTILS_HASHMAP_H
#define _KERNEL_UTILS_HASHMAP_H

#include <stdbool.h>
#include <stddef.h>

struct hashmap_entry {
    uint32_t hash;
    void* key;
    size_t key_size;
    void* value;
    struct hashmap_entry* prev;
    struct hashmap_entry* next;
};

typedef struct hashmap {
    size_t capacity;
    size_t size;
    struct hashmap_entry** entries;
} hashmap_t;

#define HASHMAP_FOREACH(table) \
	for (size_t toffset = 0; toffset < (table)->capacity; toffset++) \
		for (struct hashmap_entry* entry = (table)->entries[toffset]; entry != NULL; entry = entry->next)

hashmap_t* hashmap_create(size_t size);
void hashmap_destroy(hashmap_t* hm);
bool hashmap_get(hashmap_t* hm, const void* key, size_t key_size, void** value);
bool hashmap_set(hashmap_t* hm, const void* key, size_t key_size, void* value);
bool hashmap_remove(hashmap_t* hm, const void* key, size_t key_size);
size_t hashmap_size(hashmap_t* hm);

#endif /* _KERNEL_UTILS_HASHMAP_H */
