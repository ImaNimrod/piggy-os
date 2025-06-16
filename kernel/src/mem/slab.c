#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/string.h>

#define CACHE_NAME_MAX_LEN 64 
#define OBJECTS_PER_SLAB 256

struct slab {
    size_t available_objects;
    size_t total_objects;

    uint8_t* bitmap;
    void* buffer;

    struct slab* prev;
    struct slab* next;
    struct slab_cache* cache;
};

struct slab_cache {
    char name[CACHE_NAME_MAX_LEN + 1];
    size_t object_size;
    size_t pages_per_slab;

    struct slab* empty_slabs;
    struct slab* partial_slabs;
    struct slab* full_slabs;

    spinlock_t lock;
};

static struct slab_cache cache_cache = {0};
static struct slab_cache* kmalloc_caches[12] = {0};

static struct slab* alloc_slab(struct slab_cache* cache) {
    struct slab* new_slab = (struct slab*) (pmm_alloc_zero(cache->pages_per_slab) + HIGH_VMA);

    new_slab->available_objects = OBJECTS_PER_SLAB;
    new_slab->total_objects = OBJECTS_PER_SLAB;

    new_slab->bitmap = (uint8_t*) ((uintptr_t) new_slab + sizeof(struct slab));
    new_slab->buffer = (void*) (ALIGN_UP((uintptr_t) new_slab->bitmap + OBJECTS_PER_SLAB - HIGH_VMA, 16) + HIGH_VMA);
    new_slab->cache = cache;

    if (cache->empty_slabs) {
        cache->empty_slabs->prev = new_slab;
    }

    new_slab->prev = NULL;
    new_slab->next = cache->empty_slabs;
    cache->empty_slabs = new_slab;

    return new_slab;
}

static bool move_slab(struct slab** dest_head, struct slab** src_head, struct slab* s) {
    if (!s || !*src_head) {
        return false; 
    }

    if (s->next) {
        s->next->prev = s->prev;
    }
    if (s->prev) {
        s->prev->next = s->next;
    }
    if (*src_head == s) {
        *src_head = s->next;
    }
    if (!*dest_head) {
        s->prev = NULL;
        s->next = NULL;
        *dest_head = s; 
        return true;
    }

    s->next = *dest_head;
    s->prev = NULL;

    if (*dest_head) {
        (*dest_head)->prev = s;
    }
    *dest_head = s;

    return true;
}

static bool slab_free_object(struct slab* slab, void* object) {
    if (slab == NULL) {
        return false;
    }

    spinlock_acquire(&slab->cache->lock);

    bool ret = false;

    struct slab* root = slab;
    while (slab) {
        if ((uintptr_t) slab->buffer <= (uintptr_t) object && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) object) {
            size_t index = ((uintptr_t) object - (uintptr_t) slab->buffer) / slab->cache->object_size;
            if (BITMAP_TEST(slab->bitmap, index)) {
                BITMAP_CLEAR(slab->bitmap, index);
                slab->available_objects++;
                ret = true;
                goto end;
            }
        }

        slab = slab->next;
    }

end:
    spinlock_release(&root->cache->lock);
    return ret;
}

struct slab_cache* slab_cache_create(const char* name, size_t object_size) {
    struct slab_cache* new_cache = slab_cache_alloc(&cache_cache);
    if (new_cache == NULL) {
        return NULL;
    }

    strncpy(new_cache->name, name, CACHE_NAME_MAX_LEN);
    new_cache->object_size = object_size;
    new_cache->pages_per_slab = DIV_CEIL(object_size * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE);

    return new_cache;
}

void slab_cache_destroy(struct slab_cache* cache) {
    spinlock_acquire(&cache->lock);

    struct slab* iter;

    iter = cache->partial_slabs;
    while (iter) {
        pmm_free((uintptr_t) iter - HIGH_VMA, cache->pages_per_slab);
        iter = iter->next;
    }

    iter = cache->full_slabs;
    while (iter) {
        pmm_free((uintptr_t) iter - HIGH_VMA, cache->pages_per_slab);
        iter = iter->next;
    }

    spinlock_release(&cache->lock);
}

void* slab_cache_alloc(struct slab_cache* cache) {
    spinlock_acquire(&cache->lock);

    struct slab* slab = NULL;
    if (cache->partial_slabs) {
        slab = cache->partial_slabs;
    } else if (cache->empty_slabs) {
        slab = cache->empty_slabs;
    }

    if (!slab) {
        slab = alloc_slab(cache);
    }

    void* object = NULL;

    for (size_t i = 0; i < slab->total_objects; i++) {
        if (!BITMAP_TEST(slab->bitmap, i)) {
            BITMAP_SET(slab->bitmap, i);
            slab->available_objects--;

            object = (void*) (((uintptr_t) slab->buffer) + (i * slab->cache->object_size));
            memset8(object, 0, slab->cache->object_size);
            break;
        }
    }

    if (slab->available_objects == 0) {
        move_slab(&cache->full_slabs, &cache->partial_slabs, slab);
    } else if (slab->available_objects == (slab->total_objects - 1)) {
        move_slab(&cache->partial_slabs, &cache->empty_slabs, slab);
    }

    spinlock_release(&cache->lock);
    return object;
}

bool slab_cache_free(struct slab_cache* cache, void* object) {
    if (slab_free_object(cache->partial_slabs, object)) {
        return true;
    } else if (slab_free_object(cache->full_slabs, object)) {
        return true;
    }
    return false;
}

void slab_init(void) {
    strncpy(cache_cache.name, "struct slab_cache cache", CACHE_NAME_MAX_LEN);
    cache_cache.object_size = sizeof(struct slab_cache);
    cache_cache.pages_per_slab = DIV_CEIL(sizeof(struct slab_cache) * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE);

    kmalloc_caches[0] = slab_cache_create("kmalloc_16 cache", 16);
    kmalloc_caches[1] = slab_cache_create("kmalloc_24 cache", 24);
    kmalloc_caches[2] = slab_cache_create("kmalloc_32 cache", 32);
    kmalloc_caches[3] = slab_cache_create("kmalloc_48 cache", 48);
    kmalloc_caches[4] = slab_cache_create("kmalloc_64 cache", 64);
    kmalloc_caches[5] = slab_cache_create("kmalloc_92 cache", 92);
    kmalloc_caches[6] = slab_cache_create("kmalloc_128 cache", 128);
    kmalloc_caches[7] = slab_cache_create("kmalloc_256 cache", 256);
    kmalloc_caches[8] = slab_cache_create("kmalloc_384 cache", 384);
    kmalloc_caches[9] = slab_cache_create("kmalloc_512 cache", 512);
    kmalloc_caches[10] = slab_cache_create("kmalloc_1024 cache", 1024);
    kmalloc_caches[11] = slab_cache_create("kmalloc_2048 cache", 2048);

    klog("[slab] initialized kernel slab allocator\n");
}

void* kmalloc(size_t size) {
    if (unlikely(size == 0)) {
        return NULL;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        if (kmalloc_caches[i]->object_size >= size) {
            return slab_cache_alloc(kmalloc_caches[i]);
        }
    }

    kpanic(NULL, true, "kmalloc failed to find slab cache for object of size %zu", size);
}

void* krealloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        struct slab_cache* cache = kmalloc_caches[i];
        spinlock_acquire(&cache->lock);

        struct slab* iter = cache->partial_slabs;
        while (iter != NULL) {
            if ((uintptr_t) iter->buffer <= (uintptr_t) ptr && ((uintptr_t) iter->buffer + cache->object_size * iter->total_objects) > (uintptr_t) ptr) {
                if (cache->object_size >= size) {
                    spinlock_release(&cache->lock);
                    return ptr;
                }
            }
            iter = iter->next;
        }

        iter = cache->full_slabs;
        while (iter != NULL) {
            if ((uintptr_t) iter->buffer <= (uintptr_t) ptr && ((uintptr_t) iter->buffer + cache->object_size * iter->total_objects) > (uintptr_t) ptr) {
                if (cache->object_size >= size) {
                    spinlock_release(&cache->lock);
                    return ptr;
                }
            }
            iter = iter->next;
        }

        spinlock_release(&cache->lock);
    }

    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, size);
    }

    kfree(ptr);
    return new_ptr;
}

void kfree(void* ptr) {
    if (unlikely(ptr == NULL)) {
        return;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        if (slab_cache_free(kmalloc_caches[i], ptr)) {
            return;
        }
    }

    kpanic(NULL, true, "kfree failed to find slab cache for object 0x%lx", ptr);
}
