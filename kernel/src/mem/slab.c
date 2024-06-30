#include <mem/pmm.h>
#include <mem/vmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

#define BIG_ALLOC_HEADER_MAGIC 0xfafeceedabcdeffe
#define OBJECTS_PER_SLAB 512

struct slab {
    struct cache* cache;

    size_t available_objects;
    size_t total_objects;

    uint8_t* bitmap;
    void* buffer;

    struct slab* prev;
    struct slab* next;
};

struct big_alloc_header {
    uint64_t magic;
    size_t page_count;
    size_t size;
};

static struct cache* cache_list;
static struct cache root_cache = {0};

static struct cache* kmalloc_caches[11] = {0};

static struct slab* cache_alloc_slab(struct cache* cache) {
    struct slab* new_slab = (struct slab*) (pmm_allocz(cache->pages_per_slab) + HIGH_VMA);

    new_slab->bitmap = (uint8_t*) ((uintptr_t) new_slab + sizeof(struct slab));
    new_slab->buffer = (void*) (ALIGN_UP((uintptr_t) new_slab->bitmap + OBJECTS_PER_SLAB - HIGH_VMA, 16) + HIGH_VMA);
    new_slab->available_objects = OBJECTS_PER_SLAB;
    new_slab->total_objects = OBJECTS_PER_SLAB;
    new_slab->cache = cache;

    if (cache->empty_slabs) {
        cache->empty_slabs->prev = new_slab;
    }

    new_slab->next = cache->empty_slabs;
    cache->empty_slabs = new_slab;

    return new_slab;
}

static bool cache_move_slab(struct slab** dest_head, struct slab** src_head, struct slab* s) {
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

static void* slab_alloc_object(struct slab* slab) {
    if (slab == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < slab->total_objects; i++) {
        if (!BITMAP_TEST(slab->bitmap, i)) {
            BITMAP_SET(slab->bitmap, i);
            slab->available_objects--;

            void* object = (void*) (((uintptr_t) slab->buffer) + (i * slab->cache->object_size));
            memset(object, 0, slab->cache->object_size);
            return object;
        }
    }

    return NULL;
}

static bool slab_free_object(struct slab* slab, void* object) {
    if (slab == NULL) {
        return false;
    }

    spinlock_acquire(&slab->cache->lock);

    struct slab* root = slab;
    while (slab) {
        if ((uintptr_t) slab->buffer <= (uintptr_t) object && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) object) {
            size_t index = ((uintptr_t) object - (uintptr_t) slab->buffer) / slab->cache->object_size;
            if (BITMAP_TEST(slab->bitmap, index)) {
                BITMAP_CLEAR(slab->bitmap, index);
                slab->available_objects++;
                spinlock_release(&root->cache->lock);
                return true;
            }
        }

        slab = slab->next;
    }

    spinlock_release(&root->cache->lock);
    return false;
}

void* cache_alloc_object(struct cache* cache) {
    spinlock_acquire(&cache->lock);

    struct slab* slab = NULL;
    if (cache->partial_slabs) {
        slab = cache->partial_slabs;
    } else if (cache->empty_slabs) {
        slab = cache->empty_slabs;
    }

    if (!slab) {
        slab = cache_alloc_slab(cache);
    }

    void* addr = slab_alloc_object(slab);

    if (slab->available_objects == 0) {
        cache_move_slab(&cache->full_slabs, &cache->partial_slabs, slab);
    } else if (slab->available_objects == (slab->total_objects - 1)) {
        cache_move_slab(&cache->partial_slabs, &cache->empty_slabs, slab);
    }

    spinlock_release(&cache->lock);
    return addr;
}

bool cache_free_object(struct cache* cache, void* object) {
    if (slab_free_object(cache->partial_slabs, object)) {
        return true;
    } else if (slab_free_object(cache->full_slabs, object)) {
        return true;
    }
    return false;
}

struct cache* slab_cache_create(const char* name, size_t object_size) {
    struct cache* new_cache = cache_alloc_object(&root_cache);
    if (new_cache == NULL) {
        return NULL;
    }

    new_cache->name = name;
    new_cache->object_size = object_size;
    new_cache->pages_per_slab = DIV_CEIL(object_size * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE);

    new_cache->next = cache_list;
    cache_list = new_cache;

    return new_cache;
}

void slab_init(void) {
    root_cache.name = "cache cache";
    root_cache.object_size = sizeof(struct cache);
    root_cache.pages_per_slab = DIV_CEIL(sizeof(struct cache) * OBJECTS_PER_SLAB + sizeof(struct slab) + OBJECTS_PER_SLAB, PAGE_SIZE);

    cache_list = &root_cache;

    kmalloc_caches[0] = slab_cache_create("kmalloc_16 cache", 16);
    kmalloc_caches[1] = slab_cache_create("kmalloc_24 cache", 24);
    kmalloc_caches[2] = slab_cache_create("kmalloc_32 cache", 32);
    kmalloc_caches[3] = slab_cache_create("kmalloc_48 cache", 48);
    kmalloc_caches[4] = slab_cache_create("kmalloc_64 cache", 64);
    kmalloc_caches[5] = slab_cache_create("kmalloc_92 cache", 92);
    kmalloc_caches[6] = slab_cache_create("kmalloc_128 cache", 128);
    kmalloc_caches[7] = slab_cache_create("kmalloc_256 cache", 256);
    kmalloc_caches[8] = slab_cache_create("kmalloc_512 cache", 512);
    kmalloc_caches[9] = slab_cache_create("kmalloc_1024 cache", 1024);
    kmalloc_caches[10] = slab_cache_create("kmalloc_2048 cache", 2048);
}

void* kmalloc(size_t size) {
    if (!size) {
        return NULL;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        if (kmalloc_caches[i]->object_size >= size) {
            return cache_alloc_object(kmalloc_caches[i]);
        }
    }

    size_t page_count = DIV_CEIL(size, PAGE_SIZE);
    uintptr_t ret = pmm_alloc(page_count + 1);
    if (!ret) {
        return NULL;
    }

    ret += HIGH_VMA;

    struct big_alloc_header* header = (struct big_alloc_header*) ret;
    header->magic = BIG_ALLOC_HEADER_MAGIC;
    header->page_count = page_count;
    header->size = size;

    return (void*) (ret + PAGE_SIZE);
}

void* krealloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }

    if (!((uintptr_t) ptr & 0xfff)) {
        struct big_alloc_header* header = (struct big_alloc_header*) ((uintptr_t) ptr - PAGE_SIZE);
        if (DIV_CEIL(header->size, PAGE_SIZE) == DIV_CEIL(size, PAGE_SIZE)) {
            header->size = size;
            return ptr;
        }

        void* new_ptr = kmalloc(size);
        if (new_ptr == NULL) {
            return NULL;
        }

        if (header->size > size) {
            memcpy(new_ptr, ptr, size);
        } else {
            memcpy(new_ptr, ptr, header->size);
        }

        kfree(ptr);
        return new_ptr;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        struct cache* cache = kmalloc_caches[i];
        spinlock_acquire(&cache->lock);

        struct slab* slab = cache->partial_slabs;
        while (slab) {
            if ((uintptr_t) slab->buffer <= (uintptr_t) ptr && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) ptr) {
                if (cache->object_size >= size) {
                    spinlock_release(&cache->lock);
                    return ptr;
                }
            }
            slab = slab->next;
        }

        slab = cache->full_slabs;
        while (slab) {
            if ((uintptr_t) slab->buffer <= (uintptr_t) ptr && ((uintptr_t) slab->buffer + slab->cache->object_size * slab->total_objects) > (uintptr_t) ptr) {
                if (cache->object_size >= size) {
                    spinlock_release(&cache->lock);
                    return ptr;
                }
            }
            slab = slab->next;
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
    if (ptr == NULL) {
        return;
    }

    if (!((uintptr_t) ptr & 0xfff)) {
        struct big_alloc_header* header = (struct big_alloc_header*) ((uintptr_t) ptr - PAGE_SIZE);
        pmm_free((uintptr_t) ptr - HIGH_VMA, header->page_count + 1);
        return;
    }

    for (size_t i = 0; i < SIZEOF_ARRAY(kmalloc_caches); i++) {
        if (cache_free_object(kmalloc_caches[i], ptr)) {
            return;
        }
    }
}
