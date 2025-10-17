#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <mem/slab.h> 
#include <utils/panic.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

static struct slab_cache* file_cache;

static int get_free_fd(struct process* process, int start_fd) {
    int fd = -EMFILE;

    for (int i = start_fd; i < PROCESS_FD_COUNT; i++) {
        if (process->fds[i].file == NULL) {
            fd = i;
            break;
        }
    }

    return fd;
}

struct file* file_create(struct vfs_node* node, int flags) {
    if (unlikely(file_cache == NULL)) {
        file_cache = slab_cache_create("struct file cache", sizeof(struct file));
        if (unlikely(file_cache == NULL)) {
            kpanic(NULL, false, "failed to create object cache for files");
        }
    }

    struct file* file = slab_cache_alloc(file_cache);
    if (unlikely(file == NULL)) {
        return NULL;
    }

    file->node = node;
    file->flags = flags;
    file->refcount = 1;

    return file;
}

int file_dup(struct process* process, int old_fd, int new_fd, bool cloexec) {
    if (old_fd < 0 || old_fd >= PROCESS_FD_COUNT) {
        return -EBADF;
    }
    if (new_fd < 0 || new_fd >= PROCESS_FD_COUNT) {
        return -EBADF;
    }

    spinlock_acquire(&process->fd_lock);

    struct file* file = process->fds[old_fd].file;
    if (file == NULL) {
        spinlock_release(&process->fd_lock);
        return -EBADF;
    }

    int fd = get_free_fd(process, new_fd);
    if (fd >= 0) {
        process->fds[fd] = (struct file_descriptor) { file, cloexec };
        __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);
    }

    spinlock_release(&process->fd_lock);
    return fd;
}

void file_fork(struct process* old_process, struct process* new_process) {
    spinlock_acquire(&old_process->fd_lock);

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        if (old_process->fds[i].file == NULL) {
            continue;
        }

        new_process->fds[i] = old_process->fds[i];
        __atomic_add_fetch(&old_process->fds[i].file->refcount, 1, __ATOMIC_SEQ_CST);
    }

    spinlock_release(&old_process->fd_lock);
}

struct file* file_get(struct process* process, int fd) {
    if (fd < 0 || fd >= PROCESS_FD_COUNT) {
        return NULL;
    }

    spinlock_acquire(&process->fd_lock);

    struct file* file = process->fds[fd].file;
    if (file != NULL) {
        __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);
    }

    spinlock_release(&process->fd_lock);
    return file;
}

int file_insert(struct process* process, struct file* file, bool cloexec) {
    spinlock_acquire(&process->fd_lock);

    int fd = get_free_fd(process, 0);
    if (fd >= 0) {
        process->fds[fd] = (struct file_descriptor) { file, cloexec };
    }

    spinlock_release(&process->fd_lock);
    return fd;
}

void file_release(struct file* file) {
    if (__atomic_sub_fetch(&(file)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
        VFS_NODE_UNREF(file->node);
        slab_cache_free(file_cache, file);
    }
}

int file_resolve_dirfd(struct process* process, int dirfd, const char* path, struct file** dirfile, struct vfs_node** dirnode) {
    if (path[0] == '/') {
        VFS_NODE_REF(vfs_root);
        *dirnode = vfs_root;
    } else if (dirfd == AT_FDCWD) {
        VFS_NODE_REF(process->cwd);
        *dirnode = process->cwd;
    } else {
        *dirfile = file_get(process, dirfd);
        if (*dirfile == NULL) {
            return -EBADF;
        }

        *dirnode = (*dirfile)->node;
        if ((*dirnode)->type != VFS_TYPE_DIRECTORY) {
            file_release(*dirfile);
            return -ENOTDIR;
        }
    }

    return 0;
}
