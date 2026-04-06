#include <errno.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/elf.h>
#include <utils/macros.h>
#include <utils/random.h>
#include <utils/string.h>

#define ELFMAG "\177ELF"

#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_PAD      8

#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2
#define ELFCLASSNUM     3

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define ELFOSABI_NONE   0
#define ELFOSABI_LINUX  3

#define ET_EXEC     2
#define ET_DYN      3

#define PT_LOAD     1
#define PT_INTERP   3
#define PT_PHDR     6

#define PF_R 0x4
#define PF_W 0x2
#define PF_X 0x1

struct elf_header {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf_program_header {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static bool elf_verify(struct elf_header* header) {
    if (memcmp(header->e_ident, ELFMAG, 4)) {
        return false;
    }

    if (header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_ident[EI_DATA] != ELFDATA2LSB || header->e_ident[EI_OSABI] != ELFOSABI_NONE) {
        return false;
    }

    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        return false;
    }

    if (header->e_machine != 0x3e || header->e_version != 1) {
        return false;
    }

    return true;
}

int elf_load(struct vmm_context* vmm_context, uintptr_t load_base, struct vfs_node* node, struct auxvals* auxvals, char** interpreter) {
    if (node->type != VFS_TYPE_REGULAR) {
        return -EACCES;
    }

    int ret;

    node->ops->lock(node);

    struct elf_header header;
    if ((ret = node->ops->read(node, &header, sizeof(struct elf_header), 0, 0)) < 0) {
        goto end;
    }

    if (!elf_verify(&header)) {
        ret = -ENOEXEC;
        goto end;
    }

    auxvals->at_phdr.type = AT_PHDR;
    auxvals->at_phent.type = AT_PHENT;
    auxvals->at_phnum.type = AT_PHNUM;
    auxvals->at_pagesz.type = AT_PAGESZ;
    auxvals->at_entry.type = AT_ENTRY;
    auxvals->at_null = (struct auxval) { .type = AT_NULL, .value = 0 };

    struct elf_program_header pheader;

    for (size_t i = 0; i < header.e_phnum; i++) {
        if ((ret = node->ops->read(node, (void*) &pheader, header.e_phentsize, header.e_phoff + (i * header.e_phentsize), 0)) < 0) {
            goto end;
        }

        switch (pheader.p_type) {
            case PT_LOAD:
                size_t misalign = pheader.p_vaddr & (PAGE_SIZE_4KB - 1);
                size_t page_count = DIV_CEIL(pheader.p_memsz + misalign, PAGE_SIZE_4KB);

                uintptr_t paddr = pmm_alloc(page_count);

                int prot = PROT_READ;
                if (pheader.p_flags & PF_W) {
                    prot |= PROT_WRITE;
                }
                if (pheader.p_flags & PF_X) {
                    prot |= PROT_EXEC;
                }

                vmm_map(vmm_context, load_base + ALIGN_DOWN(pheader.p_vaddr, PAGE_SIZE_4KB), page_count * PAGE_SIZE_4KB,
                        prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, NULL, 0, paddr);

                if ((ret = node->ops->read(node, (void*) (paddr + HIGH_VMA + misalign), pheader.p_filesz, pheader.p_offset, 0)) < 0) {
                    goto end;
                }

                if (pheader.p_memsz > pheader.p_filesz) {
                    memset((void*) (paddr + HIGH_VMA + misalign + pheader.p_filesz), 0, pheader.p_memsz - pheader.p_filesz);
                }
                break;
            case PT_INTERP:
                char* buf = kmalloc(pheader.p_filesz);
                if (unlikely(buf == NULL)) {
                    ret = -ENOMEM;
                    goto end;
                }

                if ((ret = node->ops->read(node, buf, pheader.p_filesz, pheader.p_offset, 0)) < 0) {
                    goto end;
                }

                *interpreter = buf;
                break;
            case PT_PHDR:
                auxvals->at_phdr.value = pheader.p_vaddr;
                break;
        }
    }

    auxvals->at_phent.value = header.e_phentsize;
    auxvals->at_phnum.value = header.e_phnum;
    auxvals->at_pagesz.value = PAGE_SIZE_4KB;
    auxvals->at_entry.value = header.e_entry + load_base;

    ret = 0;
end:
    node->ops->unlock(node);
    return ret;
}

uintptr_t elf_setup_stack(uintptr_t stack_top_vaddr, uintptr_t stack_top_paddr, char* execfn, char* argv[], char* envp[], struct auxvals* auxvals) {
    uint64_t* stack_top = (uint64_t*) (stack_top_paddr + HIGH_VMA);
    uint64_t* stack = stack_top;

    int envp_len;
    for (envp_len = 0; envp[envp_len] != NULL; envp_len++) {
        size_t length = strlen(envp[envp_len]);
        stack = (uint64_t*) ((uintptr_t) stack - length - 1);
        memcpy(stack, envp[envp_len], length + 1);
    }

    int argv_len;
    for (argv_len = 0; argv[argv_len] != NULL; argv_len++) {
        size_t length = strlen(argv[argv_len]);
        stack = (uint64_t*) ((uintptr_t) stack - length - 1);
        memcpy(stack, argv[argv_len], length + 1);
    }

    size_t execfn_length = strlen(execfn);
    stack = (uint64_t*) ((uintptr_t) stack - execfn_length - 1);
    uint64_t* stack_execfn = stack;
    memcpy(stack, execfn, execfn_length + 1);

    stack -= 2;
    uint64_t* stack_random = stack;
    stack_random[0] = rand64();
    stack_random[1] = rand64();

    stack = (uint64_t*) ALIGN_DOWN((uintptr_t) stack, 16);
    if (((argv_len + envp_len + 1) & 1) != 0) {
        stack--;
    }

    auxvals->at_execfn = (struct auxval) { .type = AT_EXECFN, .value = stack_top_vaddr - ((uintptr_t) stack_top - (uintptr_t) stack_execfn) };
    auxvals->at_random = (struct auxval) { .type = AT_RANDOM, .value = stack_top_vaddr - ((uintptr_t) stack_top - (uintptr_t) stack_random) };
    auxvals->at_secure = (struct auxval) { .type = AT_SECURE, .value = 0 };

    size_t auxval_size = sizeof(struct auxvals) >> 3;
    stack -= auxval_size;
    memcpy64(stack, (uint64_t*) auxvals, auxval_size);

    uintptr_t old_rsp = stack_top_vaddr;

    *(--stack) = 0;
    stack -= envp_len;
    for (int i = 0; i < envp_len; i++) {
        old_rsp -= strlen(envp[i]) + 1;
        stack[i] = old_rsp;
    }

    *(--stack) = 0;
    stack -= argv_len;
    for (int i = 0; i < argv_len; i++) {
        old_rsp -= strlen(argv[i]) + 1;
        stack[i] = old_rsp;
    }

    *(--stack) = argv_len;

    return stack_top_vaddr - ((uintptr_t) stack_top - (uintptr_t) stack);
}
