#include <mem/pmm.h>
#include <sys/elf.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

bool elf_load(struct vfs_node* node, struct pagemap* pagemap, uintptr_t load_base, uintptr_t* entry) {
    struct elf_header header;
    if (node->read(node, &header, 0, sizeof(header)) < 0) {
        return false;
    }

    if (memcmp(header.e_ident, ELFMAG, 4) != 0) {
        return false;
    }

    if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
            header.e_ident[EI_OSABI] != 0 || header.e_machine != 62) {
        return false;
    }

    struct elf_program_header pheader;

    for (size_t i = 0; i < header.e_phnum; i++) {
        if (node->read(node, &pheader, header.e_phoff + (i * header.e_phentsize), sizeof(pheader)) < 0) {
            return false;
        }

        if (pheader.p_type != PT_LOAD) {
            continue;
        }

        size_t misalign = pheader.p_vaddr & (PAGE_SIZE - 1);
        size_t page_count = (misalign + pheader.p_memsz + (PAGE_SIZE - 1)) / PAGE_SIZE;

        uintptr_t phys_pages = pmm_allocz(page_count);
        if (!phys_pages) {
            return false;
        }

        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        if (pheader.p_flags & PF_W) {
            vmm_flags |= PTE_WRITABLE;
        }

        if (!(pheader.p_flags & PF_X)) {
            vmm_flags |= PTE_NX;
        }

        for (size_t j = 0; j < page_count; j++) {
            uintptr_t vaddr = load_base + pheader.p_vaddr + (j * PAGE_SIZE);
            uintptr_t paddr = phys_pages + (j * PAGE_SIZE);
            pagemap->map_page(pagemap, vaddr, paddr, vmm_flags);
        }

        if (node->read(node, (void*) (phys_pages + HIGH_VMA + misalign), pheader.p_offset, pheader.p_filesz) < 0) {
            return false;
        }
    }

    if (entry) {
        *entry = header.e_entry + load_base;
    }

    return true;
}
