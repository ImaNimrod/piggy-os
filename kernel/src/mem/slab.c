#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/math.h>
#include <utils/string.h>

#define OBJECTS_PER_SLAB 512

#define KMALLOC_MIN_CACHE_ORDER 5
#define KMALLOC_MAX_CACHE_ORDER 12

struct slab {
    size_t available_objects;
    size_t total_objects;

    uint8_t* bitmap;
    void* buffer;

    struct slab* prev;
    struct slab* next;

    struct cache* cache;
};

static struct cache cache_cache;
static struct cache* kmalloc_caches[KMALLOC_MAX_CACHE_ORDER - KMALLOC_MIN_CACHE_ORDER + 1];

static inline size_t pow2_roundup(size_t a) {
    a--;
    a |= a >> 1;
    a |= a >> 2;
    a |= a >> 4;
    a |= a >> 8;
    a |= a >> 16;
    a++;

    return a;
}

static struct slab* cache_alloc_slab(struct cache* cache) {
    struct slab* new_slab = (struct slab*) (pmm_alloc(cache->pages_per_slab) + HIGH_VMA);

    new_slab->available_objects = OBJECTS_PER_SLAB;
    new_slab->total_objects = OBJECTS_PER_SLAB;

    new_slab->bitmap = (uint8_t*) ((uintptr_t) new_slab + sizeof(struct slab));
    new_slab->buffer = (void*) (ALIGN_UP((uintptr_t) new_slab->bitmap + OBJECTS_PER_SLAB - HIGH_VMA, 16) + HIGH_VMA);

    new_slab->cache = cache;

    if (cache->empty_slabs) {
        cache->empty_slabs->prev = new_slab;
    }

    new_slab->next = cache->empty_slabs;
    cache->empty_slabs = new_slab;

    return new_slab;
}

static int cache_move_slab(struct slab** dest_head, struct slab** src_head, struct slab* src) {
    if (!src || !*src_head) {
        return -1; 
    }

    if (src->next != NULL) {
        src->next->prev = src->prev;
    }
    if (src->prev != NULL) {
        src->prev->next = src->next;
    }
    if (*src_head == src) {
        *src_head = src->next;
    }

    if (!*dest_head) {
        src->prev = NULL;
        src->next = NULL;

        *dest_head = src; 
        return 0;
    }

    src->next = *dest_head;
    src->prev = NULL;

    if (*dest_head) {
        (*dest_head)->prev = src;
    }

    *dest_head = src;

    return 0;
}

static void* slab_alloc_object(struct slab* slab) {
    for (size_t i = 0; i < slab->total_objects; i++) {
        if (!(slab->bitmap[i / 8] & (1 << i % 8))) {
            slab->bitmap[i / 8] |= (1 << (i % 8));
            slab->available_objects--;

            void* object = (void*) ((uintptr_t) slab->buffer + (i * slab->cache->object_size));
            memset(object, 0, slab->cache->object_size);
            return object;
        }
    }

    return NULL;
}

static bool slab_free_object(struct slab* slab, void* object) {
    if (!slab) {
        return true;
    }

    while (slab) {
        uintptr_t slab_buffer_end = (uintptr_t) slab->buffer + (slab->cache->object_size * slab->total_objects);

        if ((uintptr_t) slab->buffer <= (uintptr_t) object && slab_buffer_end > (uintptr_t) object) {
            size_t index = ((uintptr_t) object - (uintptr_t) slab->buffer) / slab->cache->object_size;

            if (slab->bitmap[index / 8] & (1 << index % 8)) {
                slab->bitmap[index / 8] &= ~(1 << (index % 8));
                slab->available_objects++;
                return true;
            }
        }

        slab = slab->next;
    }

    return false;
}

void* cache_alloc_object(struct cache* cache) {
    struct slab* slab = NULL;

    if (cache->partial_slabs) {
        slab = cache->partial_slabs;
    } else if (cache->empty_slabs) {
        slab = cache->empty_slabs;
    }

    if (!slab) {
        slab = cache_alloc_slab(cache);
        cache->empty_slabs = slab;
    }

    void* addr = slab_alloc_object(slab);

    if (slab->available_objects == 0) {
        cache_move_slab(&cache->full_slabs, &cache->partial_slabs, slab);
    } else if (slab->available_objects == (slab->total_objects - 1)) {
        cache_move_slab(&cache->partial_slabs, &cache->empty_slabs, slab);
    }

    if (cache->ctor) {
        cache->ctor(addr);
    }

    return addr;
}

bool cache_free_object(struct cache* cache, void* object) {
    if (cache->dtor) {
        cache->dtor(object);
    }

    if (slab_free_object(cache->partial_slabs, object)) {
        return true;
    } else if (slab_free_object(cache->full_slabs, object)) {
        return true;
    }

    return false;
}

struct cache* cache_create(const char* name, size_t object_size, void (*ctor)(void*), void (*dtor)(void*)) {
    struct cache* new_cache = cache_alloc_object(&cache_cache);
    if (!new_cache) {
        return new_cache;
    }

    *new_cache = (struct cache) {
        .name = name,
            .object_size = object_size,
            .pages_per_slab = DIV_CEIL(object_size * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE),
            .ctor = ctor,
            .dtor = dtor,
    };

    struct slab* root_slab = cache_alloc_slab(new_cache);

    root_slab->cache = new_cache;
    root_slab->buffer = (void*) ((uintptr_t) root_slab->buffer + sizeof(struct cache));
    root_slab->available_objects -= DIV_CEIL(object_size, sizeof(struct cache));
    root_slab->total_objects = root_slab->available_objects;

    new_cache->empty_slabs = root_slab;

    struct cache* temp_cache = &cache_cache;
    while (temp_cache->next) {
        temp_cache = temp_cache->next;
    }

    temp_cache->next = new_cache;

    return new_cache;
}

void cache_destroy(struct cache* cache) {
    struct slab* slab;
    for (slab = cache->empty_slabs; slab != NULL; slab = slab->next) {
        pmm_free((uintptr_t) slab, cache->pages_per_slab);
    }

    for (slab = cache->partial_slabs; slab != NULL; slab = slab->next) {
        pmm_free((uintptr_t) slab, cache->pages_per_slab);
    }

    for (slab = cache->full_slabs; slab != NULL; slab = slab->next) {
        pmm_free((uintptr_t) slab, cache->pages_per_slab);
    }

    cache_free_object(&cache_cache, cache);
}

void slab_init(void) {
    cache_cache = (struct cache) {
        .name = "cache_cache",
            .object_size = sizeof(struct cache),
            .pages_per_slab = DIV_CEIL(sizeof(struct cache) * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE),
    };

    struct slab* root_slab = cache_alloc_slab(&cache_cache);

    root_slab->cache = &cache_cache;
    root_slab->buffer = (void*) ((uintptr_t) root_slab->buffer + sizeof(struct cache));
    root_slab->available_objects -= DIV_CEIL(cache_cache.object_size, sizeof(struct cache));
    root_slab->total_objects = root_slab->available_objects;

    cache_cache.empty_slabs = root_slab;

    for (size_t order = KMALLOC_MIN_CACHE_ORDER; order <= KMALLOC_MAX_CACHE_ORDER; order++) {
        kmalloc_caches[order - KMALLOC_MIN_CACHE_ORDER] = cache_create("kmalloc", 1u << order, NULL, NULL);
    }
}

void* kmalloc(size_t size) {
    if (!size) {
        return NULL;
    }

    size_t round_size = pow2_roundup(size);
    round_size = MAX(round_size, 1u << KMALLOC_MIN_CACHE_ORDER);

    struct cache* cache;

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        cache = kmalloc_caches[i];

        if (cache->object_size == round_size) {
            return cache_alloc_object(cache);
        }
    }

    return NULL;
}

void kfree(void* ptr) {
    if (!ptr)
        return;

    struct cache* cache;

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        cache = kmalloc_caches[i];

        if (cache_free_object(cache, ptr)) {
            return;
        }
    }
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }

    struct cache* cache;
    struct slab* slab;
    size_t object_size = 0;

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        cache = kmalloc_caches[i];

        slab = cache->partial_slabs;

        while (slab) {
            if (slab->buffer <= ptr && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) ptr) {
                object_size = slab->cache->object_size;
                break;
            }

            slab = slab->next;
        }

        if (!object_size) {
            slab = cache->full_slabs;

            while (slab) {
                if (slab->buffer <= ptr && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) ptr) {
                    object_size = slab->cache->object_size;
                    break;
                }

                slab = slab->next;
            }
        }
    }

    if (object_size >= size) {
        return ptr;
    }

    void* new_ptr = kmalloc(size);

    memcpy(new_ptr, ptr, object_size);
    kfree(ptr);

    return new_ptr;
}
