#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <utils/macros.h>

void sys_truncate(struct registers* r) {
    int fd = r->rdi;
    off_t length = r->rsi;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (length < 0) {
        r->rax = -EINVAL;
        return;
    }

    struct file* file = file_get(current_process, fd);
    if (!file) {
        r->rax = -EBADF;
        return;
    }

    off_t ret;

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
    if (node->type == VFS_TYPE_DIRECTORY) {
        ret = -EISDIR;
        goto end;
    }

    node->ops->lock(node);

    ret = node->ops->truncate(node, length);
    if (ret > 0) {
        file->offset = MIN(ret, file->offset);
    }

    node->ops->unlock(node);

end:
    file_release(file);
    r->rax = ret;
}
