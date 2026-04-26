#include <errno.h>
#include <fs/pipe.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/timer.h>
#include <utils/event.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/usercopy.h>

#define PIPE_DATA_LEN 16384

struct pipe_node {
    struct vfs_node;
    struct stat stat;

    uint8_t* data;
    size_t data_length;

    size_t read_index;
    size_t write_index;

    struct event event;

    mutex_t mutex;
};

static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int pipe_ioctl(struct vfs_node* node, int request, void* argp);
static int pipe_truncate(struct vfs_node* node, off_t length);
static int pipe_sync(struct vfs_node* node);
static int pipe_getstat(struct vfs_node* node, struct stat* stat);
static int pipe_lock(struct vfs_node* node);
static int pipe_unlock(struct vfs_node* node);
static void pipe_inactive(struct vfs_node* node);

static struct vfs_node_ops pipe_node_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .ioctl = pipe_ioctl,
    .truncate = pipe_truncate,
    .sync = pipe_sync,
    .getstat = pipe_getstat,
    .lock = pipe_lock,
    .unlock = pipe_unlock,
    .inactive = pipe_inactive,
};

static ino_t pipe_inode_counter = 1;

static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    struct pipe_node* pnode = (struct pipe_node*) node;
    uint8_t* d = buf;

    if (pnode->read_index == pnode->write_index) {
        event_trigger(&pnode->event);
        return 0;
    }

    mutex_acquire(&pnode->mutex);

    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (pnode->write_index == pnode->read_index) {
            break;
        }

        USER_MEMCPY_MAYBE_TO_USER(&d[i], &pnode->data[pnode->read_index++ % pnode->data_length], sizeof(uint8_t));
    }

    event_trigger(&pnode->event);
    mutex_release(&pnode->mutex);

    return (ssize_t) i;
}

static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    struct pipe_node* pnode = (struct pipe_node*) node;
    const uint8_t* d = buf;

    mutex_acquire(&pnode->mutex);

    for (size_t i = 0; i < count; i++) {
        while (pnode->write_index == pnode->read_index + pnode->data_length) {
            event_trigger(&pnode->event);
            mutex_release(&pnode->mutex);

            event_wait(&pnode->event, true);
            mutex_acquire(&pnode->mutex);
        }

        USER_MEMCPY_MAYBE_FROM_USER(&pnode->data[pnode->write_index++ % pnode->data_length], &d[i], sizeof(uint8_t));
    }

    event_trigger(&pnode->event);
    mutex_release(&pnode->mutex);

    return count;
}

static int pipe_ioctl(struct vfs_node* node, int request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -ENOTTY;
}

static int pipe_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EINVAL;
}

static int pipe_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int pipe_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct pipe_node*) node)->stat, sizeof(struct stat));
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
    if (unlikely(node == NULL)) {
        return -ENOMEM;
    }

    node->type = VFS_TYPE_FIFO;
    node->ops = &pipe_node_ops;
    node->refcount = 1;

    node->stat.st_ino = __atomic_add_fetch(&pipe_inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_mode = vfs_type_to_mode(node->type);
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    node->data = (uint8_t*) (pmm_alloc_zero(DIV_CEIL(PIPE_DATA_LEN, PAGE_SIZE_4KB)) + HIGH_VMA);
    node->data_length = PIPE_DATA_LEN;

    event_init(&node->event);
    mutex_init(&node->mutex);

    *ret = (struct vfs_node*) node;
    return 0;
}
