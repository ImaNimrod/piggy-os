#include <mem/slab.h>
#include <utils/hashmap.h>
#include <utils/string.h>
#include <utils/log.h>

static inline uint32_t hash_function(const void * _key) {
	uint32_t hash = 0;
	const uint8_t* key = (const uint8_t*) _key;
	int c;
	while ((c = *key++)) {
		hash = c + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}

hashmap_t* hashmap_create(size_t size) {
	hashmap_t* map = kmalloc(sizeof(hashmap_t));
    if (map == NULL) {
        return NULL;
    }

	void* data = kmalloc(sizeof(hashmap_entry_t*) * size);
    if (data == NULL) {
        kfree(map);
        return NULL;
    }

    map->size = size;
    map->entries = data;
 
	return map; 
}

void hashmap_destroy(hashmap_t* map) {
    if (!map) {
        return;
    }

    for (size_t i = 0; i < map->size; i++) {
        hashmap_entry_t * x = map->entries[i], *p;
        while (x) {
            p = x;
            x = x->next;
            kfree(p->key);
            kfree(p);
        }
    }

    kfree(map->entries);
}

bool hashmap_set(hashmap_t* map, const void* key, size_t key_size, void* value) {
    if (!map) {
        return false;
    }

    uint32_t hash = hash_function(key) % map->size;

	hashmap_entry_t* x = map->entries[hash];
	if (!x) {
		hashmap_entry_t* new_entry = kmalloc(sizeof(hashmap_entry_t));
		new_entry->key = strdup(key);
		new_entry->value = value;
		new_entry->next = NULL;
		map->entries[hash] = new_entry;
        return true;
	} else {
		hashmap_entry_t* p = NULL;
		do {
            if (!strncmp(x->key, key, key_size)) {
				x->value = value;
				return true;
			} else {
				p = x;
				x = x->next;
			}
		} while (x);

		hashmap_entry_t* new_entry = kmalloc(sizeof(hashmap_entry_t));
		new_entry->key = strdup(key);
		new_entry->value = value;
		new_entry->next = NULL;

		p->next = new_entry;
        return true;
	}
}

void* hashmap_get(hashmap_t* map, const void* key, size_t key_size) {
    if (!map) {
        return NULL;
    }

    uint32_t hash = hash_function(key) % map->size;

    hashmap_entry_t* x = map->entries[hash];
    if (!x) {
        return NULL;
    }

    do {
        if (!strncmp(x->key, key, key_size)) {
            return x->value;
        }
        x = x->next;
    } while (x);

    return NULL;
}

bool hashmap_remove(hashmap_t* hashmap, const void* key, size_t key_size) {
    (void) hashmap;
    (void) key;
    (void) key_size;
    return true;
}
