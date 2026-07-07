#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>

// TODO: if fd is less than 0, make it sync the whole VFS + file/memory mappings
void sys_sync(struct registers* r) {
    int fd = r->rdi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (!file) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = file->node;

    node->ops->lock(node);
    r->rax = node->ops->sync(node);
    node->ops->unlock(node);

    file_release(file);
}
