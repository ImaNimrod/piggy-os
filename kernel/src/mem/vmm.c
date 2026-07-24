#include <cpu/smp.h>
#include <errno.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/log.h> 
#include <utils/macros.h> 
#include <utils/string.h> 

// INTERNAL USE ONLY
#define MAP_UNMAP 0x80000000

static struct slab_cache* vmm_context_cache;
static struct slab_cache* vmm_range_cache;

// TODO: Implement copy-on-write support

static inline uint64_t mmap_prot_to_pte_flags(int prot) {
    uint64_t pte_flags = PTE_PRESENT | PTE_USER;
    if (prot & PROT_WRITE) {
        pte_flags |= PTE_WRITABLE;
    }
    if (!(prot & PROT_EXEC)) {
        pte_flags |= PTE_NX;
    }
    return pte_flags;
}

static void free_unmapped_ranges(struct vmm_context* context) {
    struct vmm_range* range = context->ranges;
    while (range) {
        if (!(range->flags & MAP_UNMAP)) {
            range = range->next;
            continue;
        }

        struct vmm_range* next = range->next;

        page_size_t unused;
        for (size_t i = 0; i < range->size; i += PAGE_SIZE_4KB) {
            uintptr_t vaddr = range->base + i;
            uintptr_t entry = pagemap_get_mapping(context->pagemap, vaddr, &unused);
            if (entry == 0) {
                continue;
            }

            if (range->flags & MAP_SHARED) {
                range->node->ops->lock(range->node);
                range->node->ops->munmap(range->node, (void*) vaddr, range->offset + i);
                range->node->ops->unlock(range->node);
            } else {
                pagemap_unmap(context->pagemap, vaddr, &unused);
                pmm_free(entry & ~PTE_FLAG_MASK, 1);
            }
        }

        if (range->prev) {
            range->prev->next = range->next;
        } else {
            context->ranges = range->next;
        }
        if (range->next) {
            range->next->prev = range->prev;
        }

        slab_cache_free(vmm_range_cache, range);
        range = next;
    }
}

static void* get_next_anon_base(struct vmm_context* context, void* addr, size_t size) {
    struct vmm_range* range = context->ranges;
    if (!addr) {
        addr = (void*) USER_START;
    }

    if (!range) {
        return addr;
    }

    if (range->base != USER_START && (uintptr_t) addr < range->base && (range->base - (uintptr_t) addr) >= size) {
        return addr;
    }

    while (range->next) {
        addr = (void*) MAX((uintptr_t) addr, range->base + range->size);

        if ((uintptr_t) addr < range->next->base) {
            size_t free = range->next->base - (uintptr_t) addr;
            if (free >= size) {
                return addr;
            }
        }

        range = range->next;
    }

    addr = (void*) MIN((uintptr_t) addr, range->base + range->size);
    if (addr != (void*) USER_END && (USER_END - (uintptr_t) addr) >= size) {
        return addr;
    }

    return NULL;
}

static struct vmm_range* get_range(struct vmm_context* context, uintptr_t addr) {
    struct vmm_range* last_fault_range = this_cpu()->last_fault_range;
    if (likely(last_fault_range)) {
        if (addr >= last_fault_range->base && addr < (last_fault_range->base + last_fault_range->size)) {
            return last_fault_range;
        }
    }

    struct vmm_range* range = context->ranges;
    while (range) {
        if (addr >= range->base && addr < (range->base + range->size)) {
            break;
        }

        range = range->next;
    }

    this_cpu()->last_fault_range = range;
    return range;
}

static void insert_range_after(struct vmm_range* range, struct vmm_range* new_range) {
    new_range->prev = range;
    new_range->next = range->next;
    if (range->next) {
        range->next->prev = new_range;
    }
    range->next = new_range;
}

static void insert_range_ordered(struct vmm_context* context, struct vmm_range* new_range) {
    struct vmm_range* range = context->ranges;
    if (unlikely(!range)) {
        context->ranges = new_range;
        new_range->prev = new_range->next = NULL;
        return;
    }

    if ((new_range->base + new_range->size) <= range->base) {
        context->ranges = new_range;
        new_range->prev = NULL;
        new_range->next = range;
        range->prev = new_range;
        return;
    }

    while (range->next) {
        if (new_range->base >= (range->base + range->size) && new_range->base < range->next->base) {
            new_range->next = range->next;
            if (new_range->next) {
                new_range->next->prev = new_range;
            }

            new_range->prev = range;
            range->next = new_range;
            return;
        }

        range = range->next;
    }

    range->next = new_range;
    new_range->prev = range;
    new_range->next = NULL;
}

static struct vmm_range* try_combine_ranges(struct vmm_range* range) {
    if (!range) {
        return NULL;
    }

    if (range->prev) {
        struct vmm_range* prev = range->prev;

        if ((prev->base + prev->size) == range->base && prev->flags == range->flags && prev->pte_flags == range->pte_flags) {
            prev->size += range->size;
            prev->next = range->next;

            if (range->next) {
                range->next->prev = prev;
            }

            slab_cache_free(vmm_range_cache, range);

            range = prev;
        }
    }

    if (range->next) {
        struct vmm_range* next = range->next;

        if ((range->base + range->size) == next->base && range->flags == next->flags && range->pte_flags == next->pte_flags) {
            range->size += next->size;
            range->next = next->next;

            if (next->next) {
                next->next->prev = range;
            }

            slab_cache_free(vmm_range_cache, next);
        }
    }

    return range;
}

static void update_pte_flags(struct vmm_context* context, uintptr_t address, size_t size, uint64_t new_pte_flags) {
    page_size_t unused;
    for (size_t i = 0; i < size; i += PAGE_SIZE_4KB) {
        uintptr_t vaddr = address + i;
        uintptr_t paddr = pagemap_get_mapping(context->pagemap, vaddr, &unused) & ~PTE_FLAG_MASK;
        if (paddr == 0) {
            continue;
        }

        pagemap_remap(context->pagemap, vaddr, new_pte_flags);
    }
}

static bool update_range(struct vmm_context* context, uintptr_t address, size_t size, uint64_t new_pte_flags, bool mark_unmap) {
    uintptr_t top = address + size;

    struct vmm_range* range = context->ranges;
    while (range && range->base < top) {
        uintptr_t range_top = range->base + range->size;
        if (range_top <= address) {
            range = range->next;
            continue;
        }

        struct vmm_range* next = range->next;

        if (range->base < address && top < range_top) {
            struct vmm_range* right = slab_cache_alloc(vmm_range_cache);
            if (unlikely(!right)) {
                return false;
            }
            memcpy(right, range, sizeof(struct vmm_range));
            right->prev = right->next = NULL;

            right->base = top;
            right->size = range_top - top;

            insert_range_after(range, right);
            try_combine_ranges(right);

            struct vmm_range* middle = slab_cache_alloc(vmm_range_cache);
            if (unlikely(!middle)) {
                return false;
            }
            memcpy(middle, range, sizeof(struct vmm_range));
            middle->prev = middle->next = NULL;

            middle->base = address;
            middle->size = size;
            middle->pte_flags = new_pte_flags;
            if (mark_unmap) {
                middle->flags |= MAP_UNMAP;
            }

            insert_range_after(range, middle);

            update_pte_flags(context, middle->base, middle->size, new_pte_flags);

            range->size = address - range->base;
            return true;
        }

        if (address <= range->base && top < range_top) {
            size_t delta = top - range->base;

            struct vmm_range* new = slab_cache_alloc(vmm_range_cache);
            if (unlikely(!new)) {
                return false;
            }
            memcpy(new, range, sizeof(struct vmm_range));
            new->prev = new->next = NULL;

            new->base = range->base;
            new->size = delta;
            new->pte_flags = new_pte_flags;
            if (mark_unmap) {
                new->flags |= MAP_UNMAP;
            }

            new->next = range;
            new->prev = range->prev;
            if (range->prev) {
                range->prev->next = new;
            } else {
                context->ranges = new;
            }
            range->prev = new;

            try_combine_ranges(new);

            update_pte_flags(context, new->base, new->size, new_pte_flags);

            range->base = top;
            range->size -= delta;
            return true;
        }

        if (address <= range->base && range_top <= top) {
            range->pte_flags = new_pte_flags;
            if (mark_unmap) {
                range->flags |= MAP_UNMAP;
            }
            update_pte_flags(context, range->base, range->size, new_pte_flags);
        } else if (range->base < address && range_top <= top) {
            size_t delta = range_top - address;
            range->size -= delta;

            struct vmm_range* new = slab_cache_alloc(vmm_range_cache);
            if (unlikely(!new)) {
                return false;
            }
            memcpy(new, range, sizeof(struct vmm_range));
            new->prev = new->next = NULL;

            new->base = address;
            new->size = delta;
            new->pte_flags = new_pte_flags;
            if (mark_unmap) {
                new->flags |= MAP_UNMAP;
            }

            insert_range_after(range, new);
            try_combine_ranges(new);

            update_pte_flags(context, new->base, new->size, new_pte_flags);
        }

        range = next;
    }

    return true;
}

struct vmm_context* vmm_context_create(void) {
    struct vmm_context* context = slab_cache_alloc(vmm_context_cache);
    if (unlikely(!context)) {
        return NULL;
    }

    context->pagemap = pagemap_create();
    if (unlikely(!context->pagemap)) {
        slab_cache_free(vmm_context_cache, context);
        return NULL;
    }

    context->ranges = NULL;

    mutex_init(&context->mutex);

    return context;
}

void vmm_context_destroy(struct vmm_context* context) {
    mutex_acquire(&context->mutex);

    struct vmm_range* range = context->ranges;
    while (range) {
        struct vmm_range* next = range->next;

        page_size_t unused;
        for (size_t i = 0; i < range->size; i += PAGE_SIZE_4KB) {
            uintptr_t vaddr = range->base + i;
            uintptr_t entry = pagemap_get_mapping(context->pagemap, vaddr, &unused);
            if (entry == 0) {
                continue;
            }

            if (range->flags & MAP_SHARED) {
                range->node->ops->lock(range->node);
                range->node->ops->munmap(range->node, (void*) vaddr, range->offset + i);
                range->node->ops->unlock(range->node);
            } else {
                pagemap_unmap(context->pagemap, vaddr, &unused);
                pmm_free(entry & ~PTE_FLAG_MASK, 1);
            }
        }

        slab_cache_free(vmm_range_cache, range);
        range = next;
    }

    pagemap_destroy(context->pagemap);

    slab_cache_free(vmm_context_cache, context);
}

struct vmm_context* vmm_context_fork(struct vmm_context* old_context) {
    struct vmm_context* new_context = vmm_context_create();
    if (unlikely(!new_context)) {
        return NULL;
    }

    mutex_acquire(&old_context->mutex);

    struct vmm_range* range = old_context->ranges;
    while (range) {
        struct vmm_range* new_range = slab_cache_alloc(vmm_range_cache);
        if (unlikely(!new_range)) {
            goto error;
        }
        memcpy(new_range, range, sizeof(struct vmm_range));
        new_range->prev = new_range->next = NULL;

        page_size_t unused;
        for (size_t i = 0; i < new_range->size; i += PAGE_SIZE_4KB) {
            uintptr_t vaddr = new_range->base + i;
            uintptr_t old_paddr = pagemap_get_mapping(old_context->pagemap, vaddr, &unused) & ~PTE_FLAG_MASK;
            if (old_paddr == 0) {
                continue;
            }

            uintptr_t new_paddr = pmm_alloc(1);
            memcpy64((uint64_t*) (new_paddr + HIGH_VMA), (uint64_t*) (old_paddr + HIGH_VMA), PAGE_SIZE_4KB >> 3);
            pagemap_map(new_context->pagemap, vaddr, new_paddr, new_range->pte_flags, PAGE_SIZE_4KB);
        }

        insert_range_ordered(new_context, new_range);

        range = range->next;
    }

    mutex_release(&old_context->mutex);
    return new_context;

error:
    mutex_release(&old_context->mutex);
    vmm_context_destroy(new_context);
    return NULL;
}

void* vmm_map(struct vmm_context* context, uintptr_t address, size_t size, int prot, int flags, struct vfs_node* node, off_t offset, uintptr_t paddr) {
    mutex_acquire(&context->mutex);

    void* ret = NULL;
    struct vmm_range* range = slab_cache_alloc(vmm_range_cache);
    if (unlikely(!range)) {
        goto end;
    }

    if (flags & MAP_FIXED) {
        range->base = address;
        range->size = size;
        range->flags = flags;
        range->pte_flags = mmap_prot_to_pte_flags(prot);

        if (!update_range(context, address, size, 0, true)) {
            goto end;
        }
        pagemap_invalidate(address, size);
        free_unmapped_ranges(context);
    } else {
        void* base = get_next_anon_base(context, (void*) address, size);
        if (!base) {
            goto end;
        }

        range->base = (uintptr_t) base;
        range->size = size;
        range->flags = flags;
        range->pte_flags = mmap_prot_to_pte_flags(prot);
    }

    if (node) {
        range->node = node;
        range->offset = offset;
        VFS_NODE_REF(node);
    }

    if (paddr != 0) {
        for (size_t i = 0; i < size; i += PAGE_SIZE_4KB) {
            pagemap_map(context->pagemap, range->base + i, paddr + i, range->pte_flags, PAGE_SIZE_4KB);
        }
    }

    ret = (void*) range->base;

    insert_range_ordered(context, range);
    try_combine_ranges(range);

end:
    mutex_release(&context->mutex);
    return ret;
}

int vmm_unmap(struct vmm_context* context, uintptr_t address, size_t size) {
    mutex_acquire(&context->mutex);

    int ret = update_range(context, address, size, 0, true) ? 0 : -ENOMEM;
    if (ret < 0) {
        goto end;
    }

    pagemap_invalidate(address, size);
    free_unmapped_ranges(context);

end:
    mutex_release(&context->mutex);
    return ret;
}

int vmm_remap(struct vmm_context* context, uintptr_t address, size_t size, int prot) {
    mutex_acquire(&context->mutex);

    uint64_t new_pte_flags = mmap_prot_to_pte_flags(prot);

    int ret = update_range(context, address, size, new_pte_flags, false) ? 0 : -ENOMEM;
    pagemap_invalidate(address, size);

    mutex_release(&context->mutex);
    return ret;
}

bool vmm_page_fault_handler(uintptr_t fault_addr, uint64_t error_code) {
    if (fault_addr < USER_START || fault_addr > USER_END) {
        return false;
    }

    // If the faulting page is already present, the fault has nothing to do with the VMM
    if (error_code & (1 << 0)) {
        return false;
    }

    fault_addr = ALIGN_DOWN(fault_addr, PAGE_SIZE_4KB);

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct vmm_context* context = current_thread->process->vmm_context;
    if (unlikely(!context)) {
        return false;
    }

    mutex_acquire(&context->mutex);

    struct vmm_range* range = get_range(context, fault_addr);

    mutex_release(&context->mutex);

    if (unlikely(!range)) {
        return false;
    }

    if (range->flags & MAP_ANONYMOUS) {
        pagemap_map(context->pagemap, fault_addr, pmm_alloc_zero(1), range->pte_flags, PAGE_SIZE_4KB);
    } else {
        range->node->ops->lock(range->node);
        range->node->ops->mmap(range->node, (void*) fault_addr, range->offset + (fault_addr - range->base), range->flags, range->pte_flags);
        range->node->ops->unlock(range->node);
    }

    return true;
}

void vmm_init(void) {
    vmm_context_cache = slab_cache_create("struct vmm_context cache", sizeof(struct vmm_context));
    if (unlikely(!vmm_context_cache)) {
        kpanic(NULL, false, "failed to create object cache for vmm_context structs");
    }

    vmm_range_cache = slab_cache_create("struct vmm_range cache", sizeof(struct vmm_range));
    if (unlikely(!vmm_range_cache)) {
        kpanic(NULL, false, "failed to create object cache for vmm_range structs");
    }

    klog("[vmm] initialized virtual memory manager\n");
}
