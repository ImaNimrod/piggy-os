#ifndef _KERNEL_MEM_VMM_H
#define _KERNEL_MEM_VMM_H

#include <fs/vfs.h>
#include <mem/paging.h> 
#include <stdint.h> 
#include <utils/spinlock.h> 

#define PROT_NONE       0x00
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define PROT_EXEC       0x04

#define MAP_FILE        0x00
#define MAP_PRIVATE     0x01
#define MAP_FIXED       0x04
#define MAP_ANONYMOUS   0x08
#define MAP_ANON        MAP_ANONYMOUS

struct vmm_range {
    uintptr_t base;
    size_t size;
    int flags;
    uint64_t pte_flags;

    struct vmm_range* prev;
    struct vmm_range* next;
};

struct vmm_context {
    struct pagemap* pagemap;
    struct vmm_range* ranges;
    spinlock_t lock;
};

struct vmm_context* vmm_context_create(void);
void vmm_context_destroy(struct vmm_context* context);
struct vmm_context* vmm_context_fork(struct vmm_context* old_context);
void* vmm_map(struct vmm_context* context, uintptr_t address, size_t size, int prot, int flags, uintptr_t paddr);
int vmm_unmap(struct vmm_context* context, uintptr_t address, size_t size);
int vmm_remap(struct vmm_context* context, uintptr_t address, size_t size, int prot);
bool vmm_page_fault_handler(uintptr_t fault_addr, uint64_t error_code);
void vmm_init(void);

#endif /* _KERNEL_MEM_VMM_H */
