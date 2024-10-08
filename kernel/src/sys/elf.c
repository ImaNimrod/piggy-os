#include <errno.h>
#include <mem/pmm.h>
#include <sys/elf.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>

int elf_load(struct vfs_node* node, struct pagemap* pagemap, uintptr_t* entry) {
    int ret = 0;

    spinlock_acquire(&node->lock);

    struct elf_header header;
    if (unlikely(node->read(node, &header, 0, sizeof(header), 0) < 0)) {
        ret = -EIO;
        goto end;
    }

    if (memcmp(header.e_ident, ELFMAG, 4) != 0) {
        ret = -ENOEXEC;
        goto end;
    }

    if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
            header.e_ident[EI_OSABI] != 0 || header.e_machine != 62) {
        ret = -ENOEXEC;
        goto end;
    }

    struct elf_program_header pheader;

    for (size_t i = 0; i < header.e_phnum; i++) {
        if (unlikely(node->read(node, &pheader, header.e_phoff + (i * header.e_phentsize), sizeof(pheader), 0) < 0)) {
            ret = -EIO;
            goto end;
        }

        if (pheader.p_type != PT_LOAD) {
            continue;
        }

        size_t misalign = pheader.p_vaddr & (PAGE_SIZE - 1);
        size_t page_count = (misalign + pheader.p_memsz + (PAGE_SIZE - 1)) / PAGE_SIZE;

        uintptr_t phys_pages = pmm_allocz(page_count);
        if (!phys_pages) {
            ret = -ENOMEM;
            goto end;
        }

        uint64_t vmm_flags = PTE_PRESENT | PTE_USER;
        if (pheader.p_flags & PF_W) {
            vmm_flags |= PTE_WRITABLE;
        }

        if (!(pheader.p_flags & PF_X)) {
            vmm_flags |= PTE_NX;
        }

        for (size_t j = 0; j < page_count; j++) {
            uintptr_t vaddr = pheader.p_vaddr + (j * PAGE_SIZE);
            uintptr_t paddr = phys_pages + (j * PAGE_SIZE);
            if (!vmm_map_page(pagemap, vaddr, paddr, vmm_flags)) {
                ret = -EFAULT;
                goto end;
            }
        }

        if (unlikely(node->read(node, (void*) (phys_pages + HIGH_VMA + misalign), pheader.p_offset, pheader.p_filesz, 0) < 0)) {
            ret = -EIO;
            goto end;
        }
    }

    if (likely(entry)) {
        *entry = header.e_entry;
    }

end:
    spinlock_release(&node->lock);
    return ret;
}
