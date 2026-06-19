#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

#include <utils/log.h>
void sys_open(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;
    int flags = r->rdx;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
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
    kpath[path_len] = '\0';

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

retry:
    struct vfs_node* node = NULL;
    struct file* file = NULL;

    ret = vfs_lookup(dirnode, kpath, 0, NULL, &node);
    if (ret == 0) {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            ret = -EEXIST;
            goto end;
        } else {
            node->ops->unlock(node);
        }
    } else if (ret == -ENOENT && (flags & O_CREAT)) {
        ret = vfs_create(dirnode, kpath, VFS_TYPE_REGULAR, &node);
        if (ret == -EEXIST && !(flags & O_EXCL)) {
            goto retry;
        } else if (ret == 0) {
            node->ops->unlock(node);
        }
    }

    if (ret < 0) {
        goto end;
    }

    if (node->type != VFS_TYPE_DIRECTORY && (flags & O_DIRECTORY)) {
        ret = -ENOTDIR;
        goto end;
    }

    if (node->type == VFS_TYPE_REGULAR && (flags & O_TRUNC)) {
        node->ops->lock(node);
        ret = node->ops->truncate(node, 0);
        node->ops->unlock(node);

        if (ret < 0) {
            goto end;
        }
    }

    file = file_create(node, flags & ~O_CLOEXEC);
    if (file == NULL) {
        ret = -ENOMEM;
        goto end;
    }

    int fd = file_insert(current_process, file, flags & O_CLOEXEC);
    if (fd < 0) {
        ret = -EMFILE;
        goto end;
    }

    if (flags & O_APPEND) {
        struct stat st;

        node->ops->lock(node);
        ret = node->ops->getstat(node, &st);
        node->ops->unlock(node);

        if (ret < 0) {
            goto end;
        }

        file->offset = st.st_size;
    }

    ret = fd;

end:
    r->rax = ret;

    if (file != NULL && ret < 0) {
        file_release(file);
    }

    if (node != NULL) {
        if (ret < 0) {
            VFS_NODE_UNREF(node);
        }
    }

    file_cleanup_dirfd(dirfile, dirnode);
    kfree(kpath);
}
