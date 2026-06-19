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

void sys_readlink(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;
    char* buf = (char*) r->rdx;
    size_t length = r->r10;

    struct thread* current_thread = this_cpu()->scheduler.current_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(buf)) {
        r->rax = -EFAULT;
        return;
    }

    int ret;

    size_t path_len;
    if ((ret = user_strlen(path, &path_len)) < 0) {
        r->rax = ret;
        return;
    }

    if (path_len == 0) {
        struct file* file = file_get(current_process, dirfd);
        if (file == NULL) {
            r->rax = -EBADF;
            return;
        }

        struct vfs_node* node = file->node;

        node->ops->lock(node);
        r->rax = node->ops->readlink(node, buf, length);
        node->ops->unlock(node);

        file_release(file);
    } else {
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
            goto end;
        }

        struct vfs_node* node;
        if ((ret = vfs_lookup(dirnode, kpath, VFS_LOOKUP_FLAG_NOFOLLOW, NULL, &node)) == 0) {
            ret = node->ops->readlink(node, buf, length);

            node->ops->unlock(node);
            VFS_NODE_UNREF(node);

            file_cleanup_dirfd(dirfile, dirnode);
        }

end:
        kfree(kpath);
        r->rax = ret;
    }
}
