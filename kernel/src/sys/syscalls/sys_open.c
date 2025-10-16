#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <types.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_open(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    size_t path_len;
    if ((ret = user_strlen(path, &path_len)) < 0) {
        r->rax = ret;
        return;
    }

    char* kpath = kmalloc(path_len + 1);
    if (unlikely(kpath == NULL)) {
        r->rax = -ENOMEM;
        return;
    }

    if ((ret = user_memcpy_from_user(kpath, path, path_len)) < 0) {
        kfree(kpath);
        r->rax = ret;
        return;
    }

    struct file* dirfile = NULL;
    struct vfs_node* dirnode = NULL;
    if ((ret = file_resolve_dirfd(current_process, dirfd, kpath, &dirfile, &dirnode)) < 0) {
        kfree(kpath);
        r->rax = ret;
        return;
    }

    struct vfs_node* node = NULL;
    struct file* file = NULL;

    ret = vfs_lookup(dirnode, kpath, false, NULL, &node);
    if (ret == 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
        ret = -EEXIST;
        goto end;
    } else if (ret == -ENOENT && (flags & O_CREAT)) {
        ret = vfs_create(dirnode, kpath, VFS_TYPE_REGULAR, &node);
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

    file = file_create(node, flags);
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
        struct stat stat;
        if ((ret = node->ops->getstat(node, &stat)) < 0) {
            goto end;
        }

        file->offset = stat.st_size;
    }

    ret = fd;

end:
    r->rax = ret;

    if (file != NULL && ret < 0) {
        file_release(file);
    }

    if (node != NULL) {
        node->ops->unlock(node);
        if (ret < 0) {
            VFS_NODE_UNREF(node);
        }
    }

    if (dirnode != NULL) {
        VFS_NODE_UNREF(dirnode);
    }
    if (dirfile != NULL) {
        file_release(dirfile);
    }

    kfree(kpath);
}
