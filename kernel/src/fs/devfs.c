#include <fs/devfs.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

struct devfs_metadata {
    size_t capacity;
    void* data;
};

static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name);
static struct vfs_node* devfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type);

static struct vfs_node* devfs_root;

static struct vfs_filesystem devfs = {
    .mount = devfs_mount,
    .create = devfs_create,
};

static ssize_t devfs_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->device;

    spinlock_acquire(&node->lock);

    size_t actual_count = count;
    if ((off_t) (offset + count) >= node->size) {
        actual_count = count - ((offset + count) - node->size);
    }

    memcpy(buf, (void*) ((uintptr_t) metadata->data + offset), actual_count);

    spinlock_release(&node->lock);
    return actual_count;
}

static ssize_t devfs_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->device;

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

static bool devfs_truncate(struct vfs_node* node, off_t length) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->device;

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


static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name) {
    (void) parent;
    (void) source;
    (void) name;
    return devfs_root;
}

static struct vfs_node* devfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, int type) {
    struct vfs_node* new_node = vfs_create_node(fs, parent, name, type);
    if (new_node == NULL) {
        return NULL;
    }

    if (type == VFS_NODE_REGULAR) {
        new_node->device = kmalloc(sizeof(struct devfs_metadata));
        if (new_node->device == NULL) {
            vfs_destroy_node(new_node);
            return NULL;
        }

        struct devfs_metadata* metadata = new_node->device;

        metadata->capacity = 4096;
        metadata->data = kmalloc(metadata->capacity);

        if (metadata->data == NULL) {
            kfree(new_node->device);
            vfs_destroy_node(new_node);
            return NULL;
        }
    }

    new_node->size = 0;

    new_node->read = devfs_read;
    new_node->write = devfs_write;
    new_node->truncate = devfs_truncate;

    return new_node;
}

bool devfs_add_device(struct device* device) {
    if (vfs_get_node(devfs_root, device->name) != NULL) {
        return false;
    }

    struct vfs_node* dev_node = vfs_create_node(&devfs, devfs_root, device->name, device->type);
    if (dev_node == NULL) {
        kpanic(NULL, "failed to create devfs node");
    }

    dev_node->device = device->private;

    if (device->read) {
        dev_node->read = device->read;
    }
    if (device->write) {
        dev_node->write = device->write;
    }
    if (device->ioctl) {
        dev_node->ioctl = device->ioctl;
    }

    spinlock_acquire(&devfs_root->lock);
    hashmap_set(devfs_root->children, dev_node->name, strlen(dev_node->name), dev_node);
    spinlock_release(&devfs_root->lock);
    return true;
}

void devfs_init(void) {
    devfs_root = devfs.create(&devfs, NULL, "", VFS_NODE_DIRECTORY);
    vfs_register_filesystem("devfs", &devfs);
}
