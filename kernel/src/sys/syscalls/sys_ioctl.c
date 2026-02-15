#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>

void sys_ioctl(struct registers* r) {
    int fd = r->rdi;
    int request = r->rsi;
    void* argp = (void*) r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    int acc_mode = file->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        r->rax = -EBADF;
        return;
    }

    struct vfs_node* node = file->node;

    node->ops->lock(node);
    r->rax = node->ops->ioctl(node, request, argp);
    node->ops->unlock(node);

    file_release(file);
}
