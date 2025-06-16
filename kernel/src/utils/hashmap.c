#include <mem/slab.h>
#include <stdint.h>
#include <utils/hashmap.h>
#include <utils/macros.h>
#include <utils/string.h>

struct hashmap_entry {
    void* key;
    void* value;
    struct hashmap_entry* next;
};

struct hashmap {
    size_t size;
    key_compare_func_t key_compare_func;
    key_dupe_func_t key_dupe_func;
    key_free_func_t key_free_func;
    key_hash_func_t key_hash_func;
    value_free_func_t value_free_func;
    struct hashmap_entry** entries;
};

bool string_key_compare(const void* key1, const void* key2) {
    return strcmp((const char*) key1, (const char*) key2) == 0;
}

void* string_key_dupe(const void* key) {
    return strdup(key);
}

void string_key_free(void* key) {
    kfree(key);
}

size_t string_key_hash(const void* key) {
    const char* str = key;

    size_t hash = 5381;
    while (*str != '\0') {
        hash = ((hash << 5) + hash) + (uint8_t) (*str++);
    }
    return hash;
}

void string_value_free(void* value) {
    kfree(value);
}

hashmap_t* hashmap_create(size_t size, key_compare_func_t key_compare_func, key_dupe_func_t key_dupe_func, key_free_func_t key_free_func, key_hash_func_t key_hash_func, value_free_func_t value_free_func) {
    hashmap_t* hm = kmalloc(sizeof(hashmap_t));
    if (unlikely(hm == NULL)) {
        return NULL;
    }

    hm->entries = kmalloc(sizeof(struct hashmap_entry*) * size);
    if (unlikely(hm->entries == NULL)) {
        kfree(hm);
        return NULL;
    }

    hm->size = size;

    hm->key_compare_func = key_compare_func;
    hm->key_dupe_func = key_dupe_func;
    hm->key_free_func = key_free_func;
    hm->key_hash_func = key_hash_func;
    hm->value_free_func = value_free_func;

    return hm;
}

void hashmap_destroy(hashmap_t* hm) {
    if (unlikely(hm != NULL)) {
        return;
    }

    for (size_t i = 0; i < hm->size; i++) {
        struct hashmap_entry* entry = hm->entries[i];
        struct hashmap_entry* next_entry;

        while (entry != NULL) {
            next_entry = entry->next;

            hm->key_free_func(entry->key);
            hm->value_free_func(entry->value);
            kfree(entry);

            entry = next_entry;
        }
    }

    kfree(hm->entries);
    kfree(hm);
}

bool hashmap_get(hashmap_t* hm, const void* key, void** value) {
    if (unlikely(hm == NULL)) {
        return NULL;
    }

    size_t index = hm->key_hash_func(key) % hm->size;

    struct hashmap_entry* entry = hm->entries[index];
    while (entry != NULL) {
        if (hm->key_compare_func(entry->key, key)) {
            *value = entry->value;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

bool hashmap_set(hashmap_t* hm, const void* key, void* value) {
    if (unlikely(hm == NULL)) {
        return false;
    }

    size_t index = hm->key_hash_func(key) % hm->size;

    struct hashmap_entry* entry1 = hm->entries[index];
    if (entry1 == NULL) {
        struct hashmap_entry* new_entry = kmalloc(sizeof(struct hashmap_entry));
        if (unlikely(new_entry == NULL)) {
            return false;
        }
        new_entry->key = hm->key_dupe_func(key);
        new_entry->value = value;
        new_entry->next = NULL;

        hm->entries[index] = new_entry;
    } else {
        struct hashmap_entry* entry2 = NULL;
        do {
            if (hm->key_compare_func(entry1->key, key)) {
                entry1->value = value;
                return true;
            } else {
                entry2 = entry1;
                entry1 = entry1->next;
            }
        } while (entry1 != NULL);

        struct hashmap_entry* new_entry = kmalloc(sizeof(struct hashmap_entry));
        if (unlikely(new_entry == NULL)) {
            return false;
        }
        new_entry->key = hm->key_dupe_func(key);
        new_entry->value = value;
        new_entry->next = NULL;

        entry2->next = new_entry;
    }

    return true;
}

// TODO: implement hashmap_remove
bool hashmap_remove(hashmap_t* hm, const void* key) {
    (void) hm;
    (void) key;
    return false;
}
