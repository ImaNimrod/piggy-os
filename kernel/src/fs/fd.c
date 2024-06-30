#include <fs/fd.h>
#include <mem/slab.h>
#include <utils/log.h>

struct file_descriptor* fd_create(struct vfs_node* node, int flags) {
    node->refcount++;

    struct file_descriptor* fd = kmalloc(sizeof(struct file_descriptor));
    if (fd == NULL) {
        node->refcount--;
        return NULL;
    }

    fd->node = node;
    fd->flags = flags;
    fd->offset = 0;
    fd->refcount = 1;
    fd->lock = (spinlock_t) {0};

    return fd;
}

bool fd_close(struct process* p, int fdnum) {
    if (fdnum < 0 || fdnum >= MAX_FDS) {
        return false;
    }

    spinlock_acquire(&p->fd_lock);

    struct file_descriptor* fd = p->file_descriptors[fdnum];
    if (fd == NULL) {
        spinlock_release(&p->fd_lock);
        return false;
    }

    if (fd->refcount-- == 1) {
        kfree(fd);
    }


    p->file_descriptors[fdnum] = NULL;

    spinlock_release(&p->fd_lock);
    return true;
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
