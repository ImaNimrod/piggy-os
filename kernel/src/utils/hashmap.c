#include <mem/slab.h>
#include <stdint.h>
#include <utils/hashmap.h>
#include <utils/macros.h>
#include <utils/string.h>

#define FNV_OFFSET_BASIS_32 2166136261U
#define FNV_PRIME_32        16777619U

struct hashmap_entry {
    uint32_t hash;
    void* key;
    size_t key_size;
    void* value;
    struct hashmap_entry* prev;
    struct hashmap_entry* next;
};

static uint32_t fnv1a_hash(const void* data, size_t length) {
    const uint8_t* bytes = (const uint8_t*) data;

    uint32_t hash = FNV_OFFSET_BASIS_32;

    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME_32;
    }

    return hash;
}

static struct hashmap_entry* get_entry(hashmap_t* hm, const void* key, size_t key_size, size_t hash) {
    struct hashmap_entry* entry = hm->entries[hash % hm->capacity];
    while (entry != NULL) {
        if (entry->key_size == key_size && entry->hash == hash && (memcmp(entry->key, key, key_size) == 0)) {
            break;
        }
        entry = entry->next;
    }

    return entry;
}

hashmap_t* hashmap_create(size_t capacity) {
    hashmap_t* hm = kmalloc(sizeof(hashmap_t));
    if (unlikely(hm == NULL)) {
        return NULL;
    }

    hm->entries = kmalloc(sizeof(struct hashmap_entry*) * capacity);
    if (unlikely(hm->entries == NULL)) {
        kfree(hm);
        return NULL;
    }

    hm->capacity = capacity;
    return hm;
}

void hashmap_destroy(hashmap_t* hm) {
    for (size_t i = 0; i < hm->capacity; i++) {
        struct hashmap_entry* entry = hm->entries[i];
        struct hashmap_entry* next_entry;

        while (entry != NULL) {
            next_entry = entry->next;

            kfree(entry->key);
            kfree(entry);

            entry = next_entry;
        }
    }

    kfree(hm->entries);
    kfree(hm);
}

bool hashmap_get(hashmap_t* hm, const void* key, size_t key_size, void** value) {
    uint32_t hash = fnv1a_hash(key, key_size);

    struct hashmap_entry* entry = get_entry(hm, key, key_size, hash);
    if (entry == NULL) {
        return false;
    }

    *value = entry->value;
    return true;
}

bool hashmap_set(hashmap_t* hm, const void* key, size_t key_size, void* value) {
    uint32_t hash = fnv1a_hash(key, key_size);

    struct hashmap_entry* entry = get_entry(hm, key, key_size, hash);
    if (entry != NULL) {
        entry->value = value;
    } else {
        struct hashmap_entry* new_entry = kmalloc(sizeof(struct hashmap_entry));
        if (unlikely(new_entry == NULL)) {
            return false;
        }

        new_entry->key = kmalloc(key_size);
        if (unlikely(new_entry->key == NULL)) {
            kfree(new_entry);
            return false;
        }

        new_entry->hash = hash;
        memcpy(new_entry->key, key, key_size);
        new_entry->key_size = key_size;
        new_entry->value = value;

        new_entry->prev = NULL;
        new_entry->next = hm->entries[hash % hm->capacity];
        if (new_entry->next != NULL) {
            new_entry->next->prev = new_entry;
        }

        hm->entries[hash % hm->capacity] = new_entry;
        hm->size++;
    }

    return true;
}

bool hashmap_remove(hashmap_t* hm, const void* key, size_t key_size) {
    uint32_t hash = fnv1a_hash(key, key_size);

    struct hashmap_entry* entry = get_entry(hm, key, key_size, hash);
    if (entry == NULL) {
        return false;
    }

    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        hm->entries[hash % hm->capacity] = entry->next;
    }

    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }

    kfree(entry->key);
    kfree(entry);

    hm->size--;
    return true;
}

size_t hashmap_size(hashmap_t* hm) {
    return hm->size;
}
