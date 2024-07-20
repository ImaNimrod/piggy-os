#include <dev/char/fbdev.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <limine.h>
#include <types.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/string.h>

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static ssize_t fbdev_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    if (count == 0) {
        return 0;
    }

    spinlock_acquire(&node->lock);

    struct limine_framebuffer* framebuffer = node->private;
    size_t size = framebuffer->pitch * framebuffer->height;

    size_t actual_count = count;
    if (offset + count > size) {
        actual_count = count - (offset + count - size);
    }

    memcpy(buf, (void*) ((uintptr_t) framebuffer->address + offset), actual_count);

    spinlock_release(&node->lock);
    return actual_count;
}

static ssize_t fbdev_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    if (count == 0) {
        return 0;
    }

    spinlock_acquire(&node->lock);

    struct limine_framebuffer* framebuffer = node->private;
    size_t size = framebuffer->pitch * framebuffer->height;

    size_t actual_count = count;
    if (offset + count > size) {
        actual_count = count - (offset + count - size);
    }

    memcpy((void*) ((uintptr_t) framebuffer->address + offset), buf, actual_count);

    spinlock_release(&node->lock);
    return actual_count;
}

// TODO: implement framebuffer device ioctl
static int fbdev_ioctl(struct vfs_node* node, uint64_t request, void* argp) {
    (void) node;
    (void) request;
    (void) argp;
    return -1;
} 

void fbdev_init(void) {
    struct limine_framebuffer_response* framebuffer_response = framebuffer_request.response;
    if (!framebuffer_response || framebuffer_response->framebuffer_count == 0) {
        klog("[fbdev] no framebuffers available\n");
        return;
    }

    char fbdev_name[4] = "fb";

    struct stat fbdev_stat = {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_blksize = 4096,
    };

    for (size_t i = 0; i < framebuffer_response->framebuffer_count; i++) {
        struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[i];

        klog("[fbdev] found framebuffer #%u with mode %ux%ux%u at 0x%x\n",
                i, framebuffer->width, framebuffer->height, framebuffer->bpp, framebuffer->address);

        // TODO: fix this hack
        fbdev_name[2] = i + '0';

        struct vfs_node* fbdev = devfs_create_device(strdup(fbdev_name));
        if (fbdev == NULL) {
            kpanic(NULL, "failed to create device for framebuffer #%u in devfs", i);
        }

        memcpy(&fbdev->stat, &fbdev_stat, sizeof(struct stat));
        fbdev->stat.st_size = framebuffer->pitch * framebuffer->height; 
        fbdev->stat.st_rdev = makedev(FBDEV_MAJ, i);
        fbdev->stat.st_blocks = DIV_CEIL(fbdev->stat.st_size, fbdev->stat.st_blksize);

        fbdev->private = framebuffer;

        fbdev->read = fbdev_read;
        fbdev->write = fbdev_write;
        fbdev->ioctl = fbdev_ioctl;
    }
}
