#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <mem/slab.h> 
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>

#define FILE_REF(file) atomic_fetch_add_explicit(&(file)->refcount, 1, memory_order_relaxed)
#define FILE_UNREF(file) atomic_fetch_sub_explicit(&(file)->refcount, 1, memory_order_release) - 1

static struct slab_cache* file_cache;

static int get_free_fd(struct process* process, int start_fd) {
    int ret = -EMFILE;

    for (int i = start_fd; i < PROCESS_FD_COUNT; i++) {
        if (!process->fds[i].file) {
            ret = i;
            break;
        }
    }

    return ret;
}

int file_close(struct process* process, int fd) {
    if (fd < 0 || fd >= PROCESS_FD_COUNT) {
        return -EBADF;
    }

    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[fd].file;
    if (likely(file)) {
        process->fds[fd].file = NULL;
    }

    mutex_release(&process->fd_mutex);

    if (likely(file)) {
        file_release(file);
    } else {
        return -EBADF;
    }

    return 0;
}

struct file* file_create(struct vfs_node* node, int flags) {
    struct file* file = slab_cache_alloc(file_cache);
    if (unlikely(!file)) {
        return NULL;
    }

    file->node = node;
    file->flags = flags;
    file->offset = 0;
    file->refcount = 1;

    return file;
}

int file_dup(struct process* process, int old_fd, int new_fd, bool exact, bool cloexec) {
    if (old_fd < 0 || old_fd >= PROCESS_FD_COUNT) {
        return -EBADF;
    }
    if (exact && (new_fd < 0 || new_fd >= PROCESS_FD_COUNT)) {
        return -EBADF;
    }

    if (exact && old_fd == new_fd) {
        return new_fd;
    }

    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[old_fd].file;
    if (!file) {
        mutex_release(&process->fd_mutex);
        return -EBADF;
    }

    int ret;

    if (exact) {
        struct file_descriptor* descriptor = &process->fds[new_fd];
        if (descriptor->file) {
            file_release(descriptor->file);
        }

        descriptor->file = file;
        descriptor->cloexec = cloexec;

        ret = new_fd;
    } else {
        int fd = get_free_fd(process, new_fd);
        if (fd >= 0) {
            process->fds[fd] = (struct file_descriptor) { file, cloexec };
        }

        ret = fd;
    }

    if (ret >= 0) {
        FILE_REF(file);
    }

    mutex_release(&process->fd_mutex);
    return ret;
}

void file_fork(struct process* old_process, struct process* new_process) {
    mutex_acquire(&old_process->fd_mutex);

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        if (!old_process->fds[i].file) {
            continue;
        }

        new_process->fds[i] = old_process->fds[i];
        FILE_REF(old_process->fds[i].file);
    }

    mutex_release(&old_process->fd_mutex);
}

struct file* file_get(struct process* process, int fd) {
    if (fd < 0 || fd >= PROCESS_FD_COUNT) {
        return NULL;
    }

    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[fd].file;
    if (file) {
        FILE_REF(file);
    }

    mutex_release(&process->fd_mutex);
    return file;
}

int file_insert(struct process* process, struct file* file, bool cloexec) {
    mutex_acquire(&process->fd_mutex);

    int fd = get_free_fd(process, 0);
    if (fd >= 0) {
        process->fds[fd] = (struct file_descriptor) { file, cloexec };
    }

    mutex_release(&process->fd_mutex);
    return fd;
}

void file_release(struct file* file) {
    if (FILE_UNREF(file) == 0) {
        struct vfs_node* node = file->node;

        if (node->ops->close) {
            node->ops->lock(node);
            node->ops->close(node, file->flags);
            node->ops->unlock(node);
        }

        VFS_NODE_UNREF(node);

        slab_cache_free(file_cache, file);
    }
}

void file_cleanup_dirfd(struct file* dirfile, struct vfs_node* dirnode) {
    if (dirfile) {
        file_release(dirfile);
    } else if (dirnode) {
        VFS_NODE_UNREF(dirnode);
    }
}

int file_resolve_dirfd(struct process* process, int dirfd, const char* path, struct file** dirfile, struct vfs_node** dirnode) {
    if (path[0] == '/') {
        *dirnode = process_get_root(process);
    } else if (dirfd == AT_FDCWD) {
        *dirnode = process_get_cwd(process);
    } else {
        *dirfile = file_get(process, dirfd);
        if (!*dirfile) {
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

void file_init(void) {
    file_cache = slab_cache_create("struct file cache", sizeof(struct file));
    if (unlikely(!file_cache)) {
        kpanic(NULL, false, "failed to create object cache for files");
    }
}
