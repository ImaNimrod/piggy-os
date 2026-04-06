#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <mem/paging.h>
#include <sys/process.h>
#include <utils/log.h>

#define VALID_FLAGS (MAP_PRIVATE | MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS)
#define VALID_PROT  (PROT_READ | PROT_WRITE | PROT_EXEC)

void sys_mmap(struct registers* r) {
    void* address = (void*) r->rdi;
    size_t size = r->rsi;
    int prot = r->rdx;
    int flags = r->r10;
    int fd = r->r8;
    off_t offset = r->r9;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (size == 0 || ((uintptr_t) address % PAGE_SIZE_4KB) != 0) {
        r->rax = -EINVAL;
        return;
    }

    if (prot & ~VALID_PROT || flags & ~VALID_FLAGS) {
        r->rax = -EINVAL;
        return;
    }

    if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED)) {
        r->rax = -EINVAL;
        return;
    }

    struct file* file = NULL;
    struct vfs_node* node = NULL;

    if (!(flags & MAP_ANONYMOUS)) {
        if ((offset % PAGE_SIZE_4KB) != 0) {
            r->rax = -EINVAL;
            return;
        }

        file = file_get(current_process, fd);
        if (file == NULL) {
            r->rax = -EBADF;
            return;
        }

        if (!(file->node->flags & VFS_FLAG_MMAP)) {
            r->rax = -ENODEV;
            goto end;
        }

        vfs_type_t type = file->node->type;
        if ((type != VFS_TYPE_REGULAR && type != VFS_TYPE_BLOCKDEV && type != VFS_TYPE_CHARDEV)
                || ((flags & MAP_SHARED) && (prot & PROT_WRITE) && !(file->flags & (O_WRONLY | O_RDWR)))) {
            r->rax = -EACCES;
            goto end;
        }

        node = file->node;
    }

    r->rax = (uintptr_t) vmm_map(current_process->vmm_context, (uintptr_t) address, size, prot, flags, node, offset, 0);

end:
    if (file != NULL) {
        file_release(file);
    }
}
