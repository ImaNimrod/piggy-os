#include <errno.h>
#include <fs/pipe.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <sys/timer.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/usercopy.h>
#include <utils/wait_queue.h>

#define PIPE_DATA_LEN 16384

struct pipe_node {
    struct vfs_node;
    struct stat stat;

    uint8_t* data;
    size_t data_length;

    size_t read_index;
    size_t write_index;

    struct wait_queue read_wq;
    struct wait_queue write_wq;
    int readers;
    int writers;

    mutex_t mutex;
};

static int _pipe_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result);
static int pipe_lookup(struct vfs_node* parent, char* name, struct vfs_node** result);
static int pipe_rename(struct vfs_node* src_dir, struct vfs_node* src, char* old_name, struct vfs_node* target_dir, char* new_name);
static int pipe_unlink(struct vfs_node* parent, char* name, struct vfs_node** result);
static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags);
static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags);
static int pipe_ioctl(struct vfs_node* node, int request, void* argp);
static int pipe_truncate(struct vfs_node* node, off_t length);
static int pipe_sync(struct vfs_node* node);
static int pipe_getstat(struct vfs_node* node, struct stat* stat);
static int pipe_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int pipe_lock(struct vfs_node* node);
static int pipe_unlock(struct vfs_node* node);
static void pipe_inactive(struct vfs_node* node);

static struct vfs_node_ops pipe_node_ops = {
    .create = _pipe_create,
    .lookup = pipe_lookup,
    .rename = pipe_rename,
    .unlink = pipe_unlink,
    .read = pipe_read,
    .write = pipe_write,
    .ioctl = pipe_ioctl,
    .truncate = pipe_truncate,
    .sync = pipe_sync,
    .getstat = pipe_getstat,
    .setstat = pipe_setstat,
    .lock = pipe_lock,
    .unlock = pipe_unlock,
    .inactive = pipe_inactive,
};

static ino_t pipe_inode_counter = 1;

static inline size_t pipe_used(struct pipe_node* p) {
    return p->write_index - p->read_index;
}

static inline size_t pipe_free(struct pipe_node* p) {
    return p->data_length - pipe_used(p);
}

static int _pipe_create(struct vfs_node* parent, char* name, vfs_type_t type, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) type;
    (void) result;
    return -ENOTSUP;
}

static int pipe_lookup(struct vfs_node* parent, char* name, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) result;
    return -ENOTSUP;
}

static int pipe_rename(struct vfs_node* src_dir, struct vfs_node* src, char* old_name, struct vfs_node* target_dir, char* new_name) {
    (void) src_dir;
    (void) src;
    (void) old_name;
    (void) target_dir;
    (void) new_name;
    return -ENOTSUP;
}

static int pipe_unlink(struct vfs_node* parent, char* name, struct vfs_node** result) {
    (void) parent;
    (void) name;
    (void) result;
    return -ENOTSUP;
}

static ssize_t pipe_read(struct vfs_node* node, void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    struct pipe_node *p = (struct pipe_node *)node;
    uint8_t *d = buf;

    mutex_acquire(&p->mutex);

    while (pipe_used(p) == 0) {
        mutex_release(&p->mutex);
        wait_queue_wait(&p->read_wq);
        mutex_acquire(&p->mutex);
    }

    size_t n = 0;
    while (n < count && pipe_used(p) > 0) {
        USER_MEMCPY_MAYBE_TO_USER(&d[n], &p->data[p->read_index % p->data_length], 1);
        p->read_index++;
        n++;
    }

    mutex_release(&p->mutex);
    wait_queue_wake_all(&p->write_wq);

    return n;
}

static ssize_t pipe_write(struct vfs_node* node, const void* buf, size_t count, off_t offset, int flags) {
    (void) offset;
    (void) flags;

    struct pipe_node* p = (struct pipe_node*) node;
    const uint8_t* d = buf;

    mutex_acquire(&p->mutex);

    size_t n = 0;
    while (n < count) {
        while (pipe_free(p) == 0) {
            mutex_release(&p->mutex);
            wait_queue_wait(&p->write_wq);
            mutex_acquire(&p->mutex);
        }

        USER_MEMCPY_MAYBE_FROM_USER(&p->data[p->write_index % p->data_length], &d[n], 1);

        p->write_index++;
        n++;
    }

    mutex_release(&p->mutex);
    wait_queue_wake_all(&p->read_wq);

    return n;
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

    wait_queue_init(&node->read_wq);
    wait_queue_init(&node->write_wq);
    mutex_init(&node->mutex);

    *ret = (struct vfs_node*) node;
    return 0;
}
