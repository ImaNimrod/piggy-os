#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <types.h>

void sys_write(struct registers* r) {
    int fd = r->rdi;
    const void* buf = (void*) r->rsi; // TODO: make safe with usercopy
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    ssize_t ret;

    int acc_mode = file->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        ret = -EBADF;
        goto end;
    }
    if (acc_mode != O_RDWR && acc_mode != O_WRONLY) {
        ret = -EBADF;
        goto end;
    }

    struct vfs_node* node = file->node;

    node->ops->lock(node);
    ret = node->ops->write(node, buf, file->offset, count, file->flags);
    node->ops->unlock(node);

    if (ret > 0) {
        file->offset += ret;
    }

end:
    file_release(file);
    r->rax = ret;
}
