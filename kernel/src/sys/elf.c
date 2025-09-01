#include <limine.h>
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

extern struct limine_module_request module_request;

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

bool elf_load(struct pagemap* pagemap, uintptr_t* entry) {
    struct limine_module_response* module_response = module_request.response;
    struct limine_file* module = module_response->modules[0];

    void* data = (void*) module->address;

    struct elf_header* header = data;

    if (!elf_verify(header)) {
        return false;
    }

    struct elf_program_header* pheader;

    for (size_t i = 0; i < header->e_phnum; i++) {
        pheader = (void*) ((uintptr_t) data + header->e_phoff + (i * header->e_phentsize));
        if (pheader->p_type != PT_LOAD) {
            continue;
        }

        size_t misalign = pheader->p_vaddr & (PAGE_SIZE_4KB - 1);
        size_t page_count = (misalign + pheader->p_memsz + (PAGE_SIZE_4KB - 1)) / PAGE_SIZE_4KB;

        uintptr_t phys_pages = pmm_alloc_zero(page_count);

        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        int prot = VMM_FLAG_PROT_READ | VMM_FLAG_PROT_EXEC;
        if (pheader->p_flags & PF_W) {
            vmm_flags |= PTE_WRITABLE;
            prot |= VMM_FLAG_PROT_WRITE;
        }

        if (!(pheader->p_flags & PF_X)) {
            vmm_flags |= PTE_NX;
            prot &= ~VMM_FLAG_PROT_EXEC;
        }

        if (unlikely(!vmm_map(pagemap, pheader->p_vaddr, (page_count * PAGE_SIZE_4KB), prot))) {
            return false;
        }

        for (size_t j = 0; j < (page_count * PAGE_SIZE_4KB); j += PAGE_SIZE_4KB) {
            uintptr_t vaddr = pheader->p_vaddr + j;
            uintptr_t paddr = phys_pages + j;
            pagemap_map(pagemap, vaddr, paddr, vmm_flags, PAGE_SIZE_4KB);
        }

        memcpy((void*) (phys_pages + HIGH_VMA + misalign), (void*) ((uintptr_t) data + pheader->p_offset), pheader->p_filesz);
    }

    if (likely(entry)) {
        *entry = header->e_entry;
    }

    return true;
}
