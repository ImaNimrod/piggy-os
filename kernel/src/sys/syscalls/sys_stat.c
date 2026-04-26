#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <utils/macros.h>
#include <utils/usercopy.h>

void sys_stat(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;
    struct stat* stat = (struct stat*) r->rdx;
    int flags = r->r10;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    if (!IS_USER_ADDRESS(stat)) {
        r->rax = -EFAULT;
        return;
    }

    if (flags & AT_EMPTY_PATH) {
        struct file* file = file_get(current_process, dirfd);
        if (file == NULL) {
            r->rax = -EBADF;
            return;
        }

        struct vfs_node* node = file->node;

        node->ops->lock(node);
        r->rax = node->ops->getstat(node, stat);
        node->ops->unlock(node);

        file_release(file);
    } else {
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
            goto end;
        }

        struct file* dirfile = NULL;
        struct vfs_node* dirnode = NULL;
        if ((ret = file_resolve_dirfd(current_process, dirfd, kpath, &dirfile, &dirnode)) < 0) {
            goto end;
        }

        struct vfs_node* node;
        if ((ret = vfs_lookup(dirnode, kpath, false, NULL, &node)) == 0) {
            ret = node->ops->getstat(node, stat);

            node->ops->unlock(node);
            file_cleanup_dirfd(dirfile, dirnode);
        }

end:
        kfree(kpath);
        r->rax = ret;
    }
}
