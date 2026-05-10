#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

void sys_seek(struct registers* r) {
    int fd = r->rdi;
    off_t offset = r->rsi;
    int whence = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    struct file* file = file_get(current_process, fd);
    if (file == NULL) {
        r->rax = -EBADF;
        return;
    }

    off_t ret;

    int acc_mode = file->flags & O_ACCMODE;
    if (acc_mode & O_PATH) {
        ret = -EBADF;
        goto end2;
    }

    struct vfs_node* node = file->node;
    if (node->type == VFS_TYPE_CHARDEV || node->type == VFS_TYPE_FIFO) {
        ret = -ESPIPE;
        goto end2;
    }

    struct stat stat;

    node->ops->lock(node);

    ret = node->ops->getstat(node, &stat);
    if (ret < 0) {
        goto end;
    }

    if (node->type == VFS_TYPE_BLOCKDEV && (offset % stat.st_blksize) != 0) {
        ret = -EINVAL;
        goto end;
    }

    off_t current_offset = file->offset;
    off_t new_offset;

    switch (whence) {
        case SEEK_CUR:
            if (__builtin_add_overflow(current_offset, offset, &new_offset)) {
                ret = -EOVERFLOW;
                goto end;
            }
            break;
        case SEEK_END:
            if (__builtin_add_overflow(stat.st_size, offset, &new_offset)) {
                ret = -EOVERFLOW;
                goto end;
            }
            break;
        case SEEK_SET:
            new_offset = offset;
            break;
        default:
            ret = -EINVAL;
            goto end;
    }

    if (new_offset < 0) {
        ret = -EINVAL;
        goto end;
    }

    file->offset = new_offset;
    ret = new_offset;

end:
    node->ops->unlock(node);
end2:
    file_release(file);
    r->rax = ret;
}
