#include <fs/devfs.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/spinlock.h>
#include <utils/string.h>

struct devfs_metadata {
    size_t capacity;
    void* data;
};

static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name);
static struct vfs_node* devfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, mode_t mode);

static struct vfs_node* devfs_root;

static struct vfs_filesystem devfs = {
    .mount = devfs_mount,
    .create = devfs_create,
};

static ssize_t devfs_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->private;

    spinlock_acquire(&node->lock);

    size_t actual_count = count;
    if ((off_t) (offset + count) >= node->stat.st_size) {
        actual_count = count - ((offset + count) - node->stat.st_size);
    }

    memcpy(buf, (void*) ((uintptr_t) metadata->data + offset), actual_count);

    spinlock_release(&node->lock);
    return actual_count;
}

static ssize_t devfs_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->private;

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

    if ((off_t) (offset + count) >= node->stat.st_size) {
        node->stat.st_size = (off_t) (offset + count);
        node->stat.st_blocks = DIV_CEIL(node->stat.st_size, node->stat.st_blksize);
    }

    spinlock_release(&node->lock);
    return count;
}

static bool devfs_truncate(struct vfs_node* node, off_t length) {
    struct devfs_metadata* metadata = (struct devfs_metadata*) node->private;

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

    node->stat.st_size = length;
    node->stat.st_blocks = DIV_CEIL(node->stat.st_size, node->stat.st_blksize);

    spinlock_release(&node->lock);
    return true;
}


static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name) {
    (void) parent;
    (void) source;
    (void) name;
    return devfs_root;
}

static struct vfs_node* devfs_create(struct vfs_filesystem* fs, struct vfs_node* parent, const char* name, mode_t mode) {
    struct vfs_node* new_node = vfs_create_node(fs, parent, name, S_ISDIR(mode));
    if (new_node == NULL) {
        return NULL;
    }

    if (S_ISREG(mode)) {
        new_node->private = kmalloc(sizeof(struct devfs_metadata));
        if (new_node->private == NULL) {
            vfs_destroy_node(new_node);
            return NULL;
        }

        struct devfs_metadata* metadata = new_node->private;

        metadata->capacity = 4096;
        metadata->data = kmalloc(metadata->capacity);

        if (metadata->data == NULL) {
            kfree(new_node->private);
            vfs_destroy_node(new_node);
            return NULL;
        }
    }

    new_node->stat.st_mode = mode;
    new_node->stat.st_size = 0;
    new_node->stat.st_blksize = 512;
    new_node->stat.st_blocks = 0;

    new_node->read = devfs_read;
    new_node->write = devfs_write;
    new_node->truncate = devfs_truncate;

    return new_node;
}

bool devfs_add_device(struct device* device) {
    if (vfs_get_node(devfs_root, device->name) != NULL) {
        return false;
    }

    struct vfs_node* dev_node = vfs_create_node(&devfs, devfs_root, device->name, S_ISDIR(device->mode));
    if (dev_node == NULL) {
        kpanic(NULL, "failed to create devfs node");
    }

    dev_node->private = device->private;

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
    devfs_root = devfs.create(&devfs, NULL, "", S_IFDIR);
    vfs_register_filesystem("devfs", &devfs);
}
