#include <cpu/isr.h>
#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/process.h>
#include <sys/timer.h>
#include <utils/usercopy.h>

#define UTIME_NOW   ((1l << 30) - 1l)
#define UTIME_OMIT  ((1l << 30) - 2l)

void sys_utime(struct registers* r) {
    int dirfd = r->rdi;
    const char* path = (const char*) r->rsi;
    const struct timespec* times = (const struct timespec*) r->rdx;
    int flags = r->r10;

    struct thread* current_thread = this_cpu()->running_thread;
    struct process* current_process = current_thread->process;

    int ret;

    struct stat stat;
    int setstat_flags = 0;

    if (times != NULL) {
        struct timespec ktimes[2];
        if ((ret = user_memcpy_from_user(ktimes, times, sizeof(struct timespec) * 2)) < 0) {
            r->rax = ret;
            return;
        }

        if (ktimes[0].tv_nsec != UTIME_OMIT) {
            if (ktimes[0].tv_nsec == UTIME_NOW) {
                ktimes[0] = time_realtime;
            }

            stat.st_atim = ktimes[0];
            setstat_flags |= VFS_STAT_ST_ATIM;
        }

        if (ktimes[1].tv_nsec != UTIME_OMIT) {
            if (ktimes[1].tv_nsec == UTIME_NOW) {
                ktimes[1] = time_realtime;
            }

            stat.st_mtim = ktimes[1];
            setstat_flags |= VFS_STAT_ST_MTIM;
        }
    } else {
        stat.st_atim = stat.st_mtim = time_realtime;
        setstat_flags |= (VFS_STAT_ST_ATIM | VFS_STAT_ST_MTIM);
    }

    if (flags & AT_EMPTY_PATH) {
        struct file* file = file_get(current_process, dirfd);
        if (file == NULL) {
            r->rax = -EBADF;
            return;
        }

        struct vfs_node* node = file->node;

        node->ops->lock(node);
        r->rax = node->ops->setstat(node, &stat, setstat_flags);
        node->ops->unlock(node);

        file_release(file);
    } else {
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
        if ((ret = vfs_lookup(dirnode, kpath, false, NULL, &node)) < 0) {
            goto end;
        }
        ret = node->ops->setstat(node, &stat, setstat_flags);
        node->ops->unlock(node);

        file_cleanup_dirfd(dirfile, dirnode);

end:
        kfree(kpath);
        r->rax = ret;
    }
}
