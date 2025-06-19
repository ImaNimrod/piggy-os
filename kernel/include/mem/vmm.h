#ifndef _KERNEL_MEM_VMM_H
#define _KERNEL_MEM_VMM_H 1

#include <mem/paging.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VMM_FLAG_PROT_READ  (1 << 0)
#define VMM_FLAG_PROT_WRITE (1 << 1)
#define VMM_FLAG_PROT_EXEC  (1 << 2)
#define VMM_FLAG_ANON       (1 << 3)
#define VMM_FLAG_FILE       (1 << 4)

struct vma {
    struct pagemap* pagemap;
    uintptr_t start;
    uintptr_t end;
    int flags;
    struct vma* next;
};

bool vmm_handle_page_fault(struct pagemap* pagemap, uintptr_t fault_addr);
bool vmm_map(struct pagemap* pagemap, uintptr_t vaddr, size_t length, int prot);

#endif /* _KERNEL_MEM_VMM_H */
