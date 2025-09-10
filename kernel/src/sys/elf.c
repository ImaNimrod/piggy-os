#include <errno.h>
#include <mem/pmm.h>
#include <sys/elf.h>
#include <utils/macros.h>
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

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6
#define PT_TLS      7
#define PT_LOOS     0x60000000
#define PT_HIOS     0x6fffffff
#define PT_LOPROC   0x70000000
#define PT_HIPROC   0x7fffffff

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

    if (header->e_type != 2 || header->e_machine != 0x3e || header->e_version != 1) {
        return false;
    }

    return true;
}

int elf_load(struct pagemap* pagemap, struct vfs_node* node, uintptr_t* entry) {
    int error;

    struct elf_header header;
    if ((error = node->ops->read(node, &header, sizeof(struct elf_header), 0)) < 0) {
        return error;
    }

    if (!elf_verify(&header)) {
        return -ENOEXEC;
    }

    struct elf_program_header pheader;

    for (size_t i = 0; i < header.e_phnum; i++) {
        if ((error = node->ops->read(node, (void*) &pheader, header.e_phentsize, header.e_phoff + (i * header.e_phentsize))) < 0) {
            return error;
        }

        if (pheader.p_type != PT_LOAD) {
            continue;
        }

        size_t misalign = pheader.p_vaddr & (PAGE_SIZE_4KB - 1);
        size_t page_count = (misalign + pheader.p_memsz + (PAGE_SIZE_4KB - 1)) / PAGE_SIZE_4KB;

        uintptr_t phys_pages = pmm_alloc_zero(page_count);

        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (pheader.p_flags & PF_W) {
            pte_flags |= PTE_WRITABLE;
        }
        if (!(pheader.p_flags & PF_X)) {
            pte_flags |= PTE_NX;
        }

        for (size_t j = 0; j < page_count; j++) {
            uintptr_t vaddr = pheader.p_vaddr + (j * PAGE_SIZE_4KB);
            uintptr_t paddr = phys_pages + (j * PAGE_SIZE_4KB);
            pagemap_map(pagemap, vaddr, paddr, pte_flags, PAGE_SIZE_4KB);
        }

        if ((error = node->ops->read(node, (void*) (phys_pages + HIGH_VMA + misalign), pheader.p_filesz, pheader.p_offset)) < 0) {
            return error;
        }
    }

    if (likely(entry)) {
        *entry = header.e_entry;
    }

    return error;
}
