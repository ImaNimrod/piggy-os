#include <fs/devfs.h>
#include <fs/vfs.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name);

static struct vfs_node* devfs_root;

static struct vfs_filesystem devfs = {
    .mount = devfs_mount,
};

static struct vfs_node* devfs_mount(struct vfs_node* parent, struct vfs_node* source, const char* name) {
    (void) parent;
    (void) source;
    (void) name;
    return devfs_root;
}

struct vfs_node* devfs_create_device(const char* name) {
    if (vfs_get_node(devfs_root, name) != NULL) {
        return NULL;
    }

    struct vfs_node* dev_node = vfs_create_node(&devfs, devfs_root, name, false);
    if (dev_node == NULL) {
        kpanic(NULL, false, "failed to create devfs node");
    }

    spinlock_acquire(&devfs_root->lock);
    hashmap_set(devfs_root->children, dev_node->name, strlen(dev_node->name), dev_node);
    spinlock_release(&devfs_root->lock);
    return dev_node;
}

void devfs_init(void) {
    devfs_root = vfs_create_node(&devfs, NULL, "dev", true);

    devfs_root->stat = (struct stat) {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFDIR,
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
    };

    vfs_register_filesystem("devfs", &devfs);
}
