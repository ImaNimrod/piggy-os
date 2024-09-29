#include <errno.h>
#include <fs/fd.h>
#include <mem/slab.h>
#include <utils/string.h>

struct file_descriptor* fd_create(struct vfs_node* node, int flags) {
    struct file_descriptor* fd = kmalloc(sizeof(struct file_descriptor));
    if (fd == NULL) {
        return NULL;
    }

    node->refcount++;

    fd->node = node;
    fd->flags = flags & FILE_FLAGS_MASK;
    fd->offset = 0;
    fd->refcount = 1;
    fd->lock = (spinlock_t) {0};

    return fd;
}

int fd_close(struct process* p, int fdnum) {
    if (fdnum < 0 || fdnum >= MAX_FDS) {
        return -EBADF;
    }

    spinlock_acquire(&p->fd_lock);

    struct file_descriptor* fd = p->file_descriptors[fdnum];
    if (fd == NULL) {
        spinlock_release(&p->fd_lock);
        return -EBADF;
    }

    if (fd->refcount-- == 1) {
        kfree(fd);
    }

    p->file_descriptors[fdnum] = NULL;

    spinlock_release(&p->fd_lock);
    return 0;
}

int fd_dup(struct process* old_process, int old_fdnum, struct process* new_process, int new_fdnum, bool exact, bool cloexec) {
    if (old_fdnum == new_fdnum && old_process == new_process) {
        return -EINVAL;
    }

    struct file_descriptor* old_fd = fd_from_fdnum(old_process, old_fdnum);
    if (old_fd == NULL) {
        return -EBADF;
    }

    struct file_descriptor* new_fd = kmalloc(sizeof(struct file_descriptor));
    if (new_fd == NULL) {
        return -ENOMEM;
    }

    memcpy(new_fd, old_fd, sizeof(struct file_descriptor));
    if (cloexec) {
        new_fd->flags |= O_CLOEXEC;
    }

    if (!exact) {
        int i;
        for (i = new_fdnum; i < MAX_FDS; i++) {
            if (!new_process->file_descriptors[i]) {
                new_process->file_descriptors[i] = new_fd;
                new_fdnum = i;
                break;
            }
        }

        if (i == MAX_FDS - 1) {
            return -EMFILE;
        }
    } else {
        if (new_process->file_descriptors[new_fdnum] != NULL) {
            fd_close(new_process, new_fdnum);
        }
        new_process->file_descriptors[new_fdnum] = new_fd;
    }

    old_fd->refcount++;
    return new_fdnum;
}

int fd_alloc_fdnum(struct process* p, struct file_descriptor* fd) {
    spinlock_acquire(&p->fd_lock);

    for (int i = 0; i < MAX_FDS; i++) {
        if (p->file_descriptors[i] == NULL) {
            p->file_descriptors[i] = fd;
            spinlock_release(&p->fd_lock);
            return i;
        }
    }

    spinlock_release(&p->fd_lock);
    return -1;
}

struct file_descriptor* fd_from_fdnum(struct process* p, int fdnum) {
    if (fdnum < 0 || fdnum >= MAX_FDS) {
        return NULL;
    }

    spinlock_acquire(&p->fd_lock);

    struct file_descriptor* fd = p->file_descriptors[fdnum];
    if (fd != NULL) {
        fd->refcount++;
    }

    spinlock_release(&p->fd_lock);
    return fd;
}
