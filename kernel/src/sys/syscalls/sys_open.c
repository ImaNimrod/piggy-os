#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <sys/process.h>
#include <types.h>

void sys_open(struct registers* r) {
    const char* path = (const char*) r->rdi; // TODO: make this safe usercopy
    int flags = r->rsi;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    struct vfs_node* node = NULL;
    struct file* file = NULL;

    VFS_NODE_REF(current_process->cwd);

    int ret = vfs_lookup(current_process->cwd, path, false, NULL, &node);
    if (ret == 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
        ret = -EEXIST;
        goto end;
    } else if (ret == -ENOENT && (flags & O_CREAT)) {
        ret = vfs_create(current_process->cwd, path, VFS_TYPE_REGULAR, &node);
    }

    if (ret < 0) {
        goto end;
    }

    if (node->type != VFS_TYPE_DIRECTORY && (flags & O_DIRECTORY)) {
        ret = -ENOTDIR;
        goto end;
    }

    if (node->type == VFS_TYPE_REGULAR && (flags & O_TRUNC)) {
        if ((ret = node->ops->truncate(node, 0)) < 0) {
            goto end;
        }
    }

    struct stat stat;
    if ((ret = node->ops->getstat(node, &stat)) < 0) {
        goto end;
    }

    file = file_create(node, flags & O_CLOEXEC);
    if (file == NULL) {
        ret = -ENOMEM;
        goto end;
    }

    int fd = file_insert(current_process, file);
    if (fd < 0) {
        ret = -EMFILE;
        goto end;
    }

    if (flags & O_APPEND) {
        file->offset = stat.st_size;
    }

    ret = fd;

end:
    if (file != NULL && ret < 0) {
        file_release(file);
    }

    if (node != NULL) {
        node->ops->unlock(node);
        if (ret < 0) {
            VFS_NODE_UNREF(node);
        }
    }

    VFS_NODE_UNREF(current_process->cwd);

    r->rax = ret;
}
