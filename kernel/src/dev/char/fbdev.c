#include <dev/char/fbdev.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <limine.h>
#include <types.h>
#include <utils/log.h>
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

    for (size_t i = 0; i < framebuffer_response->framebuffer_count; i++) {
        struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[i];

        klog("[fbdev] found framebuffer #%u with mode %ux%ux%u at 0x%x\n",
                i, framebuffer->width, framebuffer->height, framebuffer->bpp, framebuffer->address);

        struct device fb_dev = {
            .name = "fb",
            .mode = S_IFCHR,
            .private = (void*) framebuffer,
            .read = fbdev_read,
            .write = fbdev_write,
            .ioctl = fbdev_ioctl,
        };

        // TODO: fix this hack
        fb_dev.name[2] = i + '0';

        if (!devfs_add_device(&fb_dev)) {
            kpanic(NULL, "failed to add framebuffer #%u device to devfs", i);
        }
    }
}
