#include <cpu/smp.h>
#include <errno.h>
#include <fs/file.h>
#include <fs/pipe.h>
#include <fs/poll.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/signal.h>
#include <sys/timer.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/usercopy.h>
#include <utils/wait_queue.h>

#define PIPE_DATA_LEN (4 * PAGE_SIZE_4KB)

struct pipe_node {
    struct vfs_node;
    struct stat stat;

    uint8_t* data;

    size_t size;
    size_t read_index;
    size_t write_index;

    size_t readers;
    size_t writers;

    struct wait_queue read_wq;
    struct wait_queue write_wq;

    mutex_t mutex;
};

static int pipe_open(struct vfs_node* node, int flags);
static void pipe_close(struct vfs_node* node, int flags);
static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int pipe_ioctl(struct vfs_node* node, int request, void* argp);
static int pipe_truncate(struct vfs_node* node, off_t length);
static short pipe_poll(struct vfs_node* node, short events, struct poll_table* pt);
static int pipe_sync(struct vfs_node* node);
static int pipe_getstat(struct vfs_node* node, struct stat* stat);
static int pipe_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int pipe_lock(struct vfs_node* node);
static int pipe_unlock(struct vfs_node* node);
static void pipe_inactive(struct vfs_node* node);

static struct vfs_node_ops pipe_node_ops = {
    .open = pipe_open,
    .close = pipe_close,
    .read = pipe_read,
    .write = pipe_write,
    .ioctl = pipe_ioctl,
    .truncate = pipe_truncate,
    .poll = pipe_poll,
    .sync = pipe_sync,
    .getstat = pipe_getstat,
    .setstat = pipe_setstat,
    .lock = pipe_lock,
    .unlock = pipe_unlock,
    .inactive = pipe_inactive,
};

static _Atomic(ino_t) inode_counter = 1;

static inline size_t pipe_used(struct pipe_node* pnode) {
    return pnode->size;
}

static inline size_t pipe_free(struct pipe_node* pnode) {
    return PIPE_DATA_LEN - pipe_used(pnode);
}

static int pipe_open(struct vfs_node* node, int flags) {
    struct pipe_node* pnode = (struct pipe_node*) node;

    mutex_acquire(&pnode->mutex);

    if ((flags & O_RDONLY) || (flags & O_RDWR)) {
        pnode->readers++;
    }

    if ((flags & O_WRONLY) || (flags & O_RDWR)) {
        pnode->writers++;
    }

    mutex_release(&pnode->mutex);
    return 0;
}

static void pipe_close(struct vfs_node* node, int flags) {
    struct pipe_node* pnode = (struct pipe_node*) node;

    mutex_acquire(&pnode->mutex);

    if ((flags & O_RDONLY) || (flags & O_RDWR)) {
        if (--pnode->readers == 0) {
            wait_queue_wake_all(&pnode->write_wq);
        }
    }

    if ((flags & O_WRONLY) || (flags & O_RDWR)) {
        if (--pnode->writers == 0) {
            wait_queue_wake_all(&pnode->read_wq);
        }
    }

    mutex_release(&pnode->mutex);
}

static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;

    if (count == 0) {
        return 0;
    }

    struct pipe_node* pnode = (struct pipe_node*) node;
    uint8_t* d = buf;

    mutex_acquire(&pnode->mutex);

    while (pipe_used(pnode) == 0) {
        if (pnode->writers == 0) {
            mutex_release(&pnode->mutex);
            return 0;
        }

        if (flags & O_NONBLOCK) {
            mutex_release(&pnode->mutex);
            return -EAGAIN;
        }

        mutex_release(&pnode->mutex);

        int ret = wait_queue_wait(&pnode->read_wq, true);
        if (ret < 0) {
            return ret;
        }

        mutex_acquire(&pnode->mutex);
    }

    bool was_full = pnode->size == PIPE_DATA_LEN;

    size_t n = 0;

    while (n < count && pipe_used(pnode) > 0) {
        size_t used = pipe_used(pnode);
        size_t read_pos = pnode->read_index % PIPE_DATA_LEN;

        size_t chunk = MIN(count - n, used);
        size_t contiguous = PIPE_DATA_LEN - read_pos;

        chunk = MIN(chunk, contiguous);

        int ret = USER_MEMCPY_MAYBE_TO_USER(d + n, pnode->data + (pnode->read_index % PIPE_DATA_LEN), chunk);
        if (ret < 0) {
            mutex_release(&pnode->mutex);
            return n ? (ssize_t) n : ret;
        }

        pnode->read_index += chunk;
        pnode->size -= chunk;
        n += chunk;
    }

    bool is_full = pnode->size == PIPE_DATA_LEN;

    mutex_release(&pnode->mutex);

    if (was_full && !is_full) {
        wait_queue_wake_all(&pnode->write_wq);
    }

    return n;
}

static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) offset;

    if (count == 0) {
        return 0;
    }

    struct pipe_node* pnode = (struct pipe_node*) node;
    const uint8_t* d = buf;

    size_t n = 0;

    mutex_acquire(&pnode->mutex);

    while (n < count) {
        while (pipe_free(pnode) == 0) {
            if (pnode->readers == 0) {
                mutex_release(&pnode->mutex);
                signal_send_process(this_cpu()->scheduler.current_thread->process, SIGPIPE);
                return n ? (ssize_t) n : -EPIPE;
            }

            if (flags & O_NONBLOCK) {
                mutex_release(&pnode->mutex);
                return n ? (ssize_t) n : -EAGAIN;
            }

            mutex_release(&pnode->mutex);

            int ret = wait_queue_wait(&pnode->write_wq, true);
            if (ret < 0) {
                return ret;
            }

            mutex_acquire(&pnode->mutex);
        }

        size_t free = pipe_free(pnode);
        size_t write_pos = pnode->write_index % PIPE_DATA_LEN;
        size_t contiguous = PIPE_DATA_LEN - write_pos;

        size_t chunk = MIN(count - n, free);
        chunk = MIN(chunk, contiguous);

        int ret = USER_MEMCPY_MAYBE_FROM_USER(pnode->data + write_pos, d + n, chunk);

        if (ret < 0) {
            mutex_release(&pnode->mutex);
            return n ? (ssize_t) n : ret;
        }

        pnode->write_index += chunk;
        pnode->size += chunk;
        n += chunk;

        wait_queue_wake_all(&pnode->read_wq);
    }

    mutex_release(&pnode->mutex);
    return n;
}

static int pipe_ioctl(struct vfs_node* node, int request, void* argp) {
    struct pipe_node* pnode = (struct pipe_node*) node;

    if (request == FIONREAD) {
        mutex_acquire(&pnode->mutex);

        int n = (int) pipe_used(pnode);
        int ret = user_memcpy_to_user(argp, &n, sizeof(n));

        mutex_release(&pnode->mutex);
        return ret;
    }

    return -ENOTTY;
}

static int pipe_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EPERM;
}

static short pipe_poll(struct vfs_node* node, short events, struct poll_table* pt) {
    struct pipe_node* pnode = (struct pipe_node*) node;

    mutex_acquire(&pnode->mutex);

    short revents = 0;

    if (events & POLLIN) {
        if (pnode->size > 0) {
            revents |= POLLIN;
        } else if (pnode->writers == 0) {
            revents |= POLLIN | POLLHUP;
        } else {
            poll_table_add(pt, &pnode->read_wq);
        }
    }

    if (events & POLLOUT) {
        if (pnode->readers == 0) {
            revents |= POLLERR;
        } else if (pnode->size < PIPE_DATA_LEN) {
            revents |= POLLOUT;
        } else {
            poll_table_add(pt, &pnode->write_wq);
        }
    }

    mutex_release(&pnode->mutex);

    return revents;
}

static int pipe_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int pipe_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct pipe_node*) node)->stat, sizeof(struct stat));
}

static int pipe_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    (void) node;
    (void) stat;
    (void) flags;
    return -ENOTSUP;
}

static int pipe_lock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int pipe_unlock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static void pipe_inactive(struct vfs_node* node) {
    struct pipe_node* pnode = (struct pipe_node*) node;
    pmm_free((uintptr_t) pnode->data - HIGH_VMA, DIV_CEIL(PIPE_DATA_LEN, PAGE_SIZE_4KB));
    kfree(node);
}

int pipe_create(struct vfs_node** ret) {
    struct pipe_node* node = kmalloc(sizeof(struct pipe_node));
    if (unlikely(!node)) {
        return -ENOMEM;
    }

    node->type = VFS_TYPE_FIFO;
    node->ops = &pipe_node_ops;
    node->refcount = 1;

    node->stat.st_dev = 0;
    node->stat.st_ino = atomic_fetch_add_explicit(&inode_counter, 1, memory_order_relaxed);
    node->stat.st_mode = vfs_type_to_mode(node->type);
    node->stat.st_nlink = 1;
    node->stat.st_rdev = 0;
    node->stat.st_size = 0;
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_blocks = 0;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    node->data = (uint8_t*) (pmm_alloc(DIV_CEIL(PIPE_DATA_LEN, PAGE_SIZE_4KB)) + HIGH_VMA);

    node->size = 0;
    node->read_index = node->write_index = 0;
    node->readers = node->writers = 1;

    wait_queue_init(&node->read_wq);
    wait_queue_init(&node->write_wq);
    mutex_init(&node->mutex);

    *ret = (struct vfs_node*) node;
    return 0;
}
