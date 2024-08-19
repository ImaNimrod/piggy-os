#include <errno.h>
#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/time.h>
#include <types.h>
#include <utils/math.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/user_access.h>

struct tmpfs_metadata {
    ino_t inode_counter;
    dev_t dev;
};

struct tmp_node_metadata {
    size_t capacity;
    void* data;
};

static struct vfs_node* tmpfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, mode_t mode);

static uint8_t tmpfs_minor = 0;

static ssize_t tmpfs_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    struct tmp_node_metadata* node_metadata = (struct tmp_node_metadata*) node->private;

    size_t actual_count = count;
    if ((off_t) (offset + count) >= node->stat.st_size) {
        actual_count = count - ((offset + count) - node->stat.st_size);
    }

    memcpy(buf, (void*) ((uintptr_t) node_metadata->data + offset), actual_count);

    node->stat.st_atim = time_realtime;

    return actual_count;
}

static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    struct tmp_node_metadata* node_metadata = (struct tmp_node_metadata*) node->private;

    if (offset + count >= node_metadata->capacity) {
        size_t new_capacity = node_metadata->capacity;
        while (offset + count >= new_capacity) {
            new_capacity *= 2;
        }

        void* new_data = krealloc(node_metadata->data, new_capacity);
        if (new_data == NULL) {
            return -ENOMEM;
        }

        node_metadata->data = new_data;
        node_metadata->capacity = new_capacity;
    }

    memcpy((void*) ((uintptr_t) node_metadata->data + offset), buf, count);

    if ((off_t) (offset + count) >= node->stat.st_size) {
        node->stat.st_size = (off_t) (offset + count);
        node->stat.st_blocks = DIV_CEIL(node->stat.st_size, node->stat.st_blksize);
    }

    node->stat.st_atim = node->stat.st_mtim = time_realtime;

    return count;
}

static int tmpfs_truncate(struct vfs_node* node, off_t length) {
    struct tmp_node_metadata* node_metadata = (struct tmp_node_metadata*) node->private;

    if ((size_t) length > node_metadata->capacity) {
        size_t new_capacity = node_metadata->capacity;
        while (new_capacity < (size_t) length) {
            new_capacity *= 2;
        }

        void* new_data = krealloc(node_metadata->data, new_capacity);
        if (new_data == NULL) {
            return -ENOMEM;
        }

        kfree(node_metadata->data);

        node_metadata->capacity = new_capacity;
        node_metadata->data = new_data;
    }

    node->stat.st_size = length;
    node->stat.st_blocks = DIV_CEIL(node->stat.st_size, node->stat.st_blksize);
    node->stat.st_atim = node->stat.st_mtim = time_realtime;

    return 0;
}

static struct vfs_node* tmpfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name) {
    (void) source;

    struct tmpfs_metadata* metadata = kmalloc(sizeof(struct tmpfs_metadata));
    metadata->inode_counter = 1;
    metadata->dev = makedev(0, tmpfs_minor++);

    struct vfs_filesystem* tmpfs = kmalloc(sizeof(struct vfs_filesystem*));
    tmpfs->private = metadata;
    tmpfs->create = tmpfs_create;

    return tmpfs->create(tmpfs, parent, name, S_IFDIR);
}

static struct vfs_node* tmpfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, mode_t mode) {
    struct vfs_node* new_node = vfs_create_node(fs, parent, name, S_ISDIR(mode));
    if (new_node == NULL) {
        return NULL;
    }

    if (S_ISREG(mode)) {
        struct tmp_node_metadata* node_metadata = kmalloc(sizeof(struct tmp_node_metadata));
        if (node_metadata == NULL) {
            vfs_destroy_node(new_node);
            return NULL;
        }

        node_metadata->capacity = 4096;

        node_metadata->data = kmalloc(4096);
        if (node_metadata->data == NULL) {
            kfree(node_metadata);
            vfs_destroy_node(new_node);
            return NULL;
        }

        new_node->private = node_metadata;
    }

    struct tmpfs_metadata* fs_metadata = fs->private;

    new_node->stat.st_dev = fs_metadata->dev;
    new_node->stat.st_ino = fs_metadata->inode_counter++;
    new_node->stat.st_mode = mode;
    new_node->stat.st_size = 0;
    new_node->stat.st_blksize = 512;
    new_node->stat.st_blocks = 0;
    new_node->stat.st_atim = new_node->stat.st_mtim = new_node->stat.st_ctim = time_realtime;

    new_node->read = tmpfs_read;
    new_node->write = tmpfs_write;
    new_node->truncate = tmpfs_truncate;

    return new_node;
}

void tmpfs_init(void) {
    vfs_register_filesystem("tmpfs", tmpfs_mount);
}
