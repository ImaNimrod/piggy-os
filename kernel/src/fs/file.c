#include <cpu/smp.h>
#include <fs/file.h>
#include <mem/slab.h> 
#include <utils/panic.h>
#include <utils/macros.h>
#include <utils/spinlock.h>

static struct slab_cache* file_cache = NULL;

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

void file_fork(struct process* old_process, struct process* new_process) {
    spinlock_acquire(&old_process->fd_lock);

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        if (old_process->fds[i] == NULL) {
            continue;
        }

        new_process->fds[i] = old_process->fds[i];
        __atomic_add_fetch(&old_process->fds[i]->refcount, 1, __ATOMIC_SEQ_CST);
    }

    spinlock_release(&old_process->fd_lock);
}

struct file* file_get(struct process* process, int fd) {
    if (fd < 0 || fd >= PROCESS_FD_COUNT) {
        return NULL;
    }

    spinlock_acquire(&process->fd_lock);

    struct file* file = process->fds[fd];
    if (file != NULL) {
        __atomic_add_fetch(&file->refcount, 1, __ATOMIC_SEQ_CST);
    }

    spinlock_release(&process->fd_lock);
    return file;
}

int file_insert(struct process* process, struct file* file) {
    int fd = -1;

    spinlock_acquire(&process->fd_lock);

    for (int i = 0; i < PROCESS_FD_COUNT; i++) {
        if (process->fds[i] == NULL) {
            process->fds[i] = file;
            fd = i;
            break;
        }
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
