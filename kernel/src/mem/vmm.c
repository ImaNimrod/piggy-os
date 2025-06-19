#include <mem/pmm.h>
#include <mem/slab.h>
#include <mem/vmm.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>

static inline uint64_t vmm_flags_to_pte_flags(int vmm_flags) {
    uint64_t pte_flags = PTE_PRESENT | PTE_USER;

    if (vmm_flags & VMM_FLAG_PROT_WRITE) {
        pte_flags |= PTE_WRITABLE;
    }

    if (!(vmm_flags & VMM_FLAG_PROT_EXEC)) {
        pte_flags |= PTE_NX;
    }

    return pte_flags;
}

static struct vma* vaddr_to_vma(struct pagemap* pagemap, uintptr_t vaddr) {
    struct vma* iter;
    SLIST_FOREACH(pagemap->vma_list, iter) {
        if (vaddr >= iter->start && vaddr < iter->end) {
            return iter;
        }
    }

    return NULL;
}

bool vmm_handle_page_fault(struct pagemap* pagemap, uintptr_t fault_addr) {
    spinlock_acquire(&pagemap->lock);
    struct vma* vma = vaddr_to_vma(pagemap, fault_addr);
    spinlock_release(&pagemap->lock);

    if (vma == NULL) {
        return false;
    }

    pagemap_map(pagemap, fault_addr & ~0xffful, pmm_alloc(1), vmm_flags_to_pte_flags(vma->flags));

    return true;
}

bool vmm_map(struct pagemap* pagemap, uintptr_t vaddr, size_t length, int flags) {
    struct vma* vma = kmalloc(sizeof(struct vma));
    if (unlikely(vma == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for virtual memory area");
    }

    vma->pagemap = pagemap;
    vma->start = vaddr;
    vma->end = vaddr + length;
    vma->flags = flags;

    SLIST_PUSH_BACK(pagemap->vma_list, vma);
    return true;
}
