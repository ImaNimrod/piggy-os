#ifndef _KERNEL_UTILS_HASHMAP_H
#define _KERNEL_UTILS_HASHMAP_H 1

#include <stdbool.h>
#include <stddef.h>

typedef struct hashmap hashmap_t;

typedef bool (*key_compare_func_t)(const void*, const void*);
typedef void* (*key_dupe_func_t)(const void*);
typedef void (*key_free_func_t)(void*);
typedef size_t (*key_hash_func_t)(const void*);
typedef void (*value_free_func_t)(void*);

extern bool string_key_compare(const void* key1, const void* key2);
extern void* string_key_dupe(const void* key);
extern void string_key_free(void* key);
extern size_t string_key_hash(const void* key);
extern void string_value_free(void* key);

#define hashmap_create_string(size) hashmap_create((size), string_key_compare, string_key_dupe, string_key_free, string_key_hash, string_value_free)

hashmap_t* hashmap_create(size_t size, key_compare_func_t key_compare_func, key_dupe_func_t key_dupe_func, key_free_func_t key_free_func, key_hash_func_t key_hash_func, value_free_func_t value_free_func);
void hashmap_destroy(hashmap_t* hm);
bool hashmap_get(hashmap_t* hm, const void* key, void** value);
bool hashmap_set(hashmap_t* hm, const void* key, void* value);
bool hashmap_remove(hashmap_t* hm, const void* key);

#endif /* _KERNEL_UTILS_HASHMAP_H */
