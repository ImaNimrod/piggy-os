#include <fs/tmpfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <types.h>
#include <utils/log.h>
#include <utils/string.h>

struct tmpfs_metadata {
    size_t capacity;
    void* data;
};

static struct vfs_node* tmpfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name);
static struct vfs_node* tmpfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type);

static struct vfs_filesystem tmpfs = {
    .mount = tmpfs_mount,
    .create = tmpfs_create,
};

static ssize_t tmpfs_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    struct tmpfs_metadata* metadata = (struct tmpfs_metadata*) node->device;

    spinlock_acquire(&node->lock);

    size_t actual_count = count;
    if ((off_t) (offset + count) >= node->size) {
        actual_count = count - ((offset + count) - node->size);
    }

    memcpy(buf, (void*) ((uintptr_t) metadata->data + offset), actual_count);

    spinlock_release(&node->lock);
    return actual_count;
}

static ssize_t tmpfs_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    struct tmpfs_metadata* metadata = (struct tmpfs_metadata*) node->device;

    spinlock_acquire(&node->lock);

    if (offset + count >= metadata->capacity) {
        size_t new_capacity = metadata->capacity;
        while (offset + count >= new_capacity) {
            new_capacity *= 2;
        }

        void* new_data = krealloc(metadata->data, new_capacity);
        if (new_data == NULL) {
            spinlock_release(&node->lock);
            return -1;
        }

        metadata->data = new_data;
        metadata->capacity = new_capacity;
    }

    memcpy((void*) ((uintptr_t) metadata->data + offset), buf, count);

    if ((off_t) (count + offset) >= node->size) {
        node->size = (off_t) (offset + count);
    }

    spinlock_release(&node->lock);
    return count;
}

static bool tmpfs_truncate(struct vfs_node* node, off_t length) {
    struct tmpfs_metadata* metadata = (struct tmpfs_metadata*) node->device;

    spinlock_acquire(&node->lock);

    if ((size_t) length > metadata->capacity) {
        size_t new_capacity = metadata->capacity;
        while (new_capacity < (size_t) length) {
            new_capacity *= 2;
        }

        void* new_data = krealloc(metadata->data, new_capacity);
        if (new_data == NULL) {
            spinlock_release(&node->lock);
            return false;
        }

        kfree(metadata->data);

        metadata->capacity = new_capacity;
        metadata->data = new_data;
    }

    node->size = length;

    spinlock_release(&node->lock);
    return true;
}

static struct vfs_node* tmpfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name) {
    (void) source;
    struct vfs_filesystem* new_fs = &tmpfs;
    return (struct vfs_node*) new_fs->create(new_fs, parent, name, VFS_NODE_DIRECTORY);
}

static struct vfs_node* tmpfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type) {
    struct vfs_node* new_node = vfs_create_node(fs, parent, name, type);
    if (new_node == NULL) {
        return NULL;
    }

    if (type == VFS_NODE_REGULAR) {
        new_node->device = kmalloc(sizeof(struct tmpfs_metadata));
        if (new_node->device == NULL) {
            vfs_destroy_node(new_node);
            return NULL;
        }

        struct tmpfs_metadata* metadata = new_node->device;

        metadata->capacity = 4096;
        metadata->data = kmalloc(metadata->capacity);

        if (metadata->data == NULL) {
            kfree(new_node->device);
            vfs_destroy_node(new_node);
            return NULL;
        }
    }

    new_node->size = 0;

    new_node->read = tmpfs_read;
    new_node->write = tmpfs_write;
    new_node->truncate = tmpfs_truncate;

    return (struct vfs_node*) new_node;
}


void tmpfs_init(void) {
    vfs_register_filesystem("tmpfs", &tmpfs);
}