#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

#define CHUNK_MAGIC 0xeacefe32
#define HOLE_MAGIC  0xdead
#define CHUNK_MAX_SIZE 0x8000

struct heap_chunk {
    uint32_t magic;
    uint32_t size: 31;
    uint32_t free: 1;
    LIST_ENTRY(struct heap_chunk) list;
};

static uintptr_t heap_base, heap_size;
static LIST_HEAD(struct heap_chunk) chunks;
static struct heap_chunk* last_chunk;

static void* kmalloc_internal(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        kpanic("invalid alignment given: %lu\n", alignment);
    }

    if (size == 0) {
        return NULL;
    } else if (size > CHUNK_MAX_SIZE) {
        kpanic("kmalloc requested allocation too large (%lu)", size);
    }

    size = ALIGN_UP(MAX(size, 8), 8);

    if (chunks.first) {
        struct heap_chunk *chunk = NULL, *iter;
        LIST_FOREACH(iter, &chunks, list) {
            if (((uintptr_t) iter + sizeof(struct heap_chunk)) % alignment != 0) {
                continue;
            }

            if (iter->size >= size) {
                if (!chunk || (iter->size < chunk->size)) {
                    chunk = iter;
                }
            } else if (iter->size == size) {
                chunk = iter;
                break;
            }
        }

        if (chunk != NULL) {
            LIST_REMOVE(&chunks, chunk, list);
            chunk->free = false;
            return (void*) ((uintptr_t) chunk + sizeof(struct heap_chunk));
        }
    }

    uintptr_t chunk_addr;
    if (last_chunk == NULL) {
        chunk_addr = heap_base;
    } else {
        chunk_addr = (uintptr_t) last_chunk + sizeof(struct heap_chunk) + last_chunk->size;
    }

    uintptr_t aligned_mem = ALIGN_UP(chunk_addr + sizeof(struct heap_chunk), alignment);
    uintptr_t aligned_chunk = aligned_mem - sizeof(struct heap_chunk);

    if (aligned_chunk != chunk_addr) {
        size_t hole_size = aligned_chunk - chunk_addr;
        if (hole_size < sizeof(struct heap_chunk) + 8) {
            ((uint16_t*) chunk_addr)[0] = HOLE_MAGIC;
            ((uint16_t*) chunk_addr)[1] = hole_size;
        } else {
            struct heap_chunk* free_chunk = (struct heap_chunk*) chunk_addr;
            free_chunk->magic = CHUNK_MAGIC;
            free_chunk->size = hole_size - sizeof(struct heap_chunk);
            free_chunk->free = true;
            LIST_ADD_FRONT(&chunks, free_chunk, list);
        }
    }

    if (aligned_mem + size > heap_base + heap_size) {
        kpanic("kmalloc out of memory");
    }

    struct heap_chunk* chunk = (struct heap_chunk*) aligned_chunk;
    chunk->magic = CHUNK_MAGIC;
    chunk->size = size;
    chunk->free = false;
    chunk->list.next = NULL;
    chunk->list.prev = NULL;

    last_chunk = chunk;

    return (void*) ((uintptr_t) chunk + sizeof(struct heap_chunk));
}

void heap_init(void) {
    uintptr_t heap_paddr = pmm_alloc(DIV_CEIL(HEAP_SIZE, PAGE_SIZE));
    uintptr_t heap_vaddr = HEAP_VADDR;

    struct pagemap* pagemap = vmm_get_kernel_pagemap();
    for (size_t i = 0; i < DIV_CEIL(HEAP_SIZE, PAGE_SIZE); i++) {
        pagemap->map_page(pagemap, heap_vaddr + (i * PAGE_SIZE), heap_paddr + (i * PAGE_SIZE), PTE_PRESENT | PTE_WRITABLE | PTE_NX | (1 << 5) );
    }

    heap_base = heap_vaddr;
    heap_size = HEAP_SIZE;
    LIST_INIT(&chunks);
    last_chunk = NULL;
}

void* kmalloc(size_t size) {
    return kmalloc_internal(size, 4);
}

void* kmalloc_align(size_t size, size_t alignment) {
    return kmalloc_internal(size, alignment);
}

void kfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    struct heap_chunk* chunk = (struct heap_chunk*) ((uintptr_t) ptr - sizeof(struct heap_chunk));
    if (chunk->magic != CHUNK_MAGIC) {
        kpanic("kfree - invalid pointer");
        return;
    } else if (chunk->free) {
        kpanic("kfree - already freed chunk");
        return;
    }

    if (!(chunk->list.next == NULL && chunk->list.prev == NULL)) {
        kpanic("kfree - chunk linked to other chunks");
    }

    chunk->free = true;
    LIST_ADD_FRONT(&chunks, chunk, list);
}

void* kcalloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    size_t total = nmemb * size;

    void* ptr = kmalloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }

    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    struct heap_chunk* chunk = (struct heap_chunk*) ((uintptr_t) ptr - sizeof(struct heap_chunk));
    if (chunk->magic != CHUNK_MAGIC) {
        kpanic("krealloc - invalid pointer");
        return NULL;
    } else if (chunk->size >= size) {
        return ptr;
    }

    void* new_ptr = kmalloc_internal(size, 4);
    if (new_ptr) {
        memcpy(new_ptr, ptr, chunk->size);
    }

    kfree(ptr);
    return new_ptr;
}
