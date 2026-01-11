#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <mem/slab.h> 
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>

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

int file_close(struct process* process, int fd) {
    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[fd].file;
    if (file != NULL) {
        process->fds[fd].file = NULL;
    }

    mutex_release(&process->fd_mutex);

    if (file != NULL) {
        file_release(file);
    } else {
        return -EBADF;
    }

    return 0;
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

int file_dup(struct process* process, int old_fd, int new_fd, bool exact, bool cloexec) {
    if (old_fd < 0 || old_fd >= PROCESS_FD_COUNT) {
        return -EBADF;
    }
    if (exact && (new_fd < 0 || new_fd >= PROCESS_FD_COUNT)) {
        return -EBADF;
    }

    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[old_fd].file;
    if (file == NULL) {
        mutex_release(&process->fd_mutex);
        return -EBADF;
    }

    int ret;

    if (exact) {
        struct file_descriptor* descriptor = &process->fds[new_fd];
        if (descriptor->file != NULL) {
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

    __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);

    mutex_release(&process->fd_mutex);
    return ret;
}

void file_fork(struct process* old_process, struct process* new_process) {
    mutex_acquire(&old_process->fd_mutex);

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        if (old_process->fds[i].file == NULL) {
            continue;
        }

        new_process->fds[i] = old_process->fds[i];
        __atomic_add_fetch(&old_process->fds[i].file->refcount, 1, __ATOMIC_SEQ_CST);
    }

    mutex_release(&old_process->fd_mutex);
}

struct file* file_get(struct process* process, int fd) {
    if (fd < 0 || fd >= PROCESS_FD_COUNT) {
        return NULL;
    }

    mutex_acquire(&process->fd_mutex);

    struct file* file = process->fds[fd].file;
    if (file != NULL) {
        __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);
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
    if (__atomic_sub_fetch(&(file)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {
        VFS_NODE_UNREF(file->node);
        slab_cache_free(file_cache, file);
    }
}

void file_cleanup_dirfd(struct file* dirfile, struct vfs_node* dirnode) {
    if (dirnode != NULL) {
        VFS_NODE_UNREF(dirnode);
    }
    if (dirfile != NULL) {
        file_release(dirfile);
    }
}

int file_resolve_dirfd(struct process* process, int dirfd, const char* path, struct file** dirfile, struct vfs_node** dirnode) {
    if (path[0] == '/') {
        *dirnode = process_get_root(process);
    } else if (dirfd == AT_FDCWD) {
        *dirnode = process_get_cwd(process);
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
