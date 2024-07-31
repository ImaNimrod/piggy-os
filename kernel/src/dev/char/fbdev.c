#include <dev/char/fbdev.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <limine.h>
#include <mem/slab.h>
#include <sys/time.h>
#include <types.h>
#include <utils/log.h>
#include <utils/math.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/user_access.h>

volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

struct fb_variable_info {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    uint32_t red_offset;
    uint32_t red_length;
    uint32_t red_msb_right;
    uint32_t green_offset;
    uint32_t green_length;
    uint32_t green_msb_right;
    uint32_t blue_offset;
    uint32_t blue_length;
    uint32_t blue_msb_right;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct fb_fixed_info {
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

struct fb_info {
    struct limine_framebuffer* framebuffer;
    struct fb_variable_info variable;
    struct fb_fixed_info fixed;
};

static ssize_t fbdev_read(struct vfs_node* node, void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    if (count == 0) {
        return 0;
    }

    spinlock_acquire(&node->lock);

    struct fb_info* fb_info = node->private;
    size_t size = fb_info->fixed.mmio_len;

    if ((size_t) offset >= size) {
        spinlock_release(&node->lock);
        return 0;
    }

    if (count + (size_t) offset > size) {
        count = size - offset;
    }

    memcpy(buf, (void*) ((uintptr_t) fb_info->framebuffer->address + offset), count);

    spinlock_release(&node->lock);
    return count;
}

static ssize_t fbdev_write(struct vfs_node* node, const void* buf, off_t offset, size_t count, int flags) {
    (void) flags;

    if (count == 0) {
        return 0;
    }

    spinlock_acquire(&node->lock);

    struct fb_info* fb_info = node->private;
    size_t size = fb_info->fixed.mmio_len;

    if ((size_t) offset >= size) {
        spinlock_release(&node->lock);
        return 0;
    }

    if (count + (size_t) offset > size) {
        count = size - offset;
    }

    memcpy((void*) ((uintptr_t) fb_info->framebuffer->address + offset), buf, count);

    spinlock_release(&node->lock);
    return count;
}

static int fbdev_ioctl(struct vfs_node* node, uint64_t request, void* argp) {
    struct fb_info* fb_info = node->private;

    int ret = -ENOTTY;

    switch (request) {
        case FBIOBLANK:
            ret = 0;
            break;
        case FBIOGET_FSCREENINFO:
            if (copy_to_user(argp, (void*) &fb_info->fixed, sizeof(struct fb_fixed_info)) == NULL) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }
            break;
        case FBIOGET_VSCREENINFO:
            if (copy_to_user(argp, (void*) &fb_info->variable, sizeof(struct fb_variable_info)) == NULL) {
                ret = -EFAULT;
            } else {
                ret = 0;
            }
            break;
    }

    return ret;
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
        .st_atim = time_realtime,
        .st_mtim = time_realtime,
        .st_ctim = time_realtime
    };

    for (size_t i = 0; i < framebuffer_response->framebuffer_count; i++) {
        struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[i];

        klog("[fbdev] found framebuffer #%u with mode %ux%ux%u at 0x%x\n",
                i, framebuffer->width, framebuffer->height, framebuffer->bpp, framebuffer->address);

        struct fb_info* fb_info = kmalloc(sizeof(struct fb_info));
        if (fb_info == NULL) {
            kpanic(NULL, false, "failed to allocate memory for framebuffer device info");
        }

        fb_info->framebuffer = framebuffer;

        fb_info->variable.xres = framebuffer->width;
        fb_info->variable.yres = framebuffer->height;
        fb_info->variable.bits_per_pixel = framebuffer->bpp;
        fb_info->variable.red_offset = framebuffer->red_mask_shift;
        fb_info->variable.red_length = framebuffer->red_mask_size;
        fb_info->variable.red_msb_right = 1;
        fb_info->variable.green_offset = framebuffer->green_mask_shift;
        fb_info->variable.green_length = framebuffer->green_mask_size;
        fb_info->variable.green_msb_right = 1;
        fb_info->variable.blue_offset = framebuffer->blue_mask_shift;
        fb_info->variable.blue_length = framebuffer->blue_mask_size;
        fb_info->variable.blue_msb_right = 1;
		fb_info->variable.height = -1;
		fb_info->variable.width = -1;

        strncpy(fb_info->fixed.id, "LIMINE FB", sizeof(fb_info->fixed.id));
        fb_info->fixed.smem_len = fb_info->fixed.mmio_len = framebuffer->pitch * framebuffer->height;
        fb_info->fixed.type = 0;
        fb_info->fixed.visual = 2;
        fb_info->fixed.line_length = framebuffer->pitch;

        // TODO: fix this hack
        fbdev_name[2] = i + '0';

        struct vfs_node* fbdev_node = devfs_create_device(strdup(fbdev_name));
        if (fbdev_node == NULL) {
            kpanic(NULL, false, "failed to create device node for framebuffer #%u in devfs", i);
        }

        memcpy(&fbdev_node->stat, &fbdev_stat, sizeof(struct stat));
        fbdev_node->stat.st_size = framebuffer->pitch * framebuffer->height; 
        fbdev_node->stat.st_rdev = makedev(FBDEV_MAJ, i);
        fbdev_node->stat.st_blocks = DIV_CEIL(fbdev_node->stat.st_size, fbdev_node->stat.st_blksize);

        fbdev_node->private = fb_info;

        fbdev_node->read = fbdev_read;
        fbdev_node->write = fbdev_write;
        fbdev_node->ioctl = fbdev_ioctl;
    }
}
