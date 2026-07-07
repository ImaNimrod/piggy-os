#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <utils/usercopy.h>

void sys_getdents(struct registers* r) {
    int fd = r->rdi;
    void* buf = (void*) r->rsi;
    size_t count = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(buf)) {
        r->rax = -EFAULT;
        return;
    }

    size_t actual_count = count / sizeof(struct dirent);
    if (actual_count == 0) {
        r->rax = -EINVAL;
        return;
    }

    struct file* file = file_get(current_process, fd);
    if (!file) {
        r->rax = -EBADF;
        return;
    }

    ssize_t ret;

    int acc_mode = file->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        ret = -EBADF;
        goto end;
    }
    if (acc_mode != O_RDWR && acc_mode != O_RDONLY) {
        ret = -EBADF;
        goto end;
    }

    struct vfs_node* node = file->node;

    node->ops->lock(node);

    ret = node->ops->getdents(node, buf, count, file->offset);
    if (ret > 0) {
        file->offset += ret;
        ret *= sizeof(struct dirent);
    }

    node->ops->unlock(node);

end:
    file_release(file);
    r->rax = ret;
}
