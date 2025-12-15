#ifndef _KERNEL_SYS_ELF_H
#define _KERNEL_SYS_ELF_H

#include <fs/vfs.h>
#include <mem/vmm.h>
#include <stdint.h>
#include <sys/process.h>

#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_ENTRY    9

#define AT_EXECFN   15
#define AT_RANDOM   16
#define AT_SECURE   17

struct auxval {
    uint64_t type;
    uint64_t value;
};

struct auxvals {
    struct auxval at_execfn;
    struct auxval at_random;
    struct auxval at_secure;

    struct auxval at_phdr;
    struct auxval at_phent;
    struct auxval at_phnum;
    struct auxval at_pagesz;
    struct auxval at_entry;
    struct auxval at_null;
};

int elf_load(struct vmm_context* vmm_context, uintptr_t load_base, struct vfs_node* node, struct auxvals* auxvals, char** interpreter);
void elf_setup_stack(struct thread* thread, uintptr_t stack_top_paddr, char* execfn, char* argv[], char* envp[], struct auxvals* auxvals);

#endif /* _KERNEL_SYS_ELF_H */
