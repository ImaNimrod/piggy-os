#include <dev/char/fb.h>
#include <errno.h>
#include <fs/devfs.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>
#include <utils/usercopy.h>

#include "../../utils/flanterm/src/flanterm_backends/fb.h"
#include "../../utils/printf/printf.h"

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_fix_screeninfo {
    char id[16];
    uintptr_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uintptr_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
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

struct framebuffer_info {
    uintptr_t address;
    struct fb_fix_screeninfo fix_info;
    struct fb_var_screeninfo var_info;
};

extern struct limine_framebuffer_request framebuffer_request;

struct flanterm_context* fb_context;

static ssize_t fb_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);
static ssize_t fb_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);
static int fb_ioctl(dev_t dev, int request, void* argp);

static struct device_ops fb_ops = {
    .read = fb_read,
    .write = fb_write,
    .ioctl = fb_ioctl,
};

static struct framebuffer_info* framebuffers;
static size_t framebuffer_count;

static void* flanterm_alloc(size_t size) {
    return (void*) (pmm_alloc(DIV_CEIL(size, PAGE_SIZE_4KB)) + HIGH_VMA);
}

static void flanterm_free(void* ptr, size_t size) {
    pmm_free((uintptr_t) ptr - HIGH_VMA, DIV_CEIL(size, PAGE_SIZE_4KB));
}

static ssize_t fb_read(dev_t dev, void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    dev_t minor = minor(dev);
    if ((unsigned) minor >= framebuffer_count) {
        return -ENODEV;
    }

    struct framebuffer_info* framebuffer = &framebuffers[minor];

    ssize_t end = framebuffer->fix_info.mmio_len;
    if (offset >= end) {
        return 0;
    }

    ssize_t actual_count = count;
    if (actual_count + offset > end) {
        actual_count = end - offset;
    }

    ssize_t ret = USER_MEMCPY_MAYBE_TO_USER(buf, (void*) (framebuffer->address + offset), actual_count);
    if (ret < 0) {
        return ret;
    }

    return actual_count;
}

static ssize_t fb_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) flags;

    dev_t minor = minor(dev);
    if ((unsigned) minor >= framebuffer_count) {
        return -ENODEV;
    }

    struct framebuffer_info* framebuffer = &framebuffers[minor];

    ssize_t end = framebuffer->fix_info.mmio_len;
    if (offset >= end) {
        return 0;
    }

    ssize_t actual_count = count;
    if (actual_count + offset > end) {
        actual_count = end - offset;
    }

    ssize_t ret = USER_MEMCPY_MAYBE_FROM_USER((void*) (framebuffer->address + offset), buf, actual_count);
    if (ret < 0) {
        return ret;
    }

    return actual_count;
}

static int fb_ioctl(dev_t dev, int request, void* argp) {
    dev_t minor = minor(dev);
    if (minor >= framebuffer_count) {
        return -ENODEV;
    }

    struct framebuffer_info* framebuffer = &framebuffers[minor];

    int ret = 0;

    switch (request) {
        case FBIOGET_VSCREENINFO:
            ret = user_memcpy_to_user(argp, &framebuffer->var_info, sizeof(struct fb_var_screeninfo));
            break;
        case FBIOPUT_VSCREENINFO:
            break;
        case FBIOGET_FSCREENINFO:
            ret = user_memcpy_to_user(argp, &framebuffer->fix_info, sizeof(struct fb_fix_screeninfo));
            break;
        case FBIOBLANK:
            break;
        default:
            ret = -ENOTTY;
            break;
    }

    return ret;
}

void fb_dev_init(void) {
    struct limine_framebuffer_response* framebuffer_response = framebuffer_request.response;
    if (unlikely(framebuffer_response->framebuffer_count == 0)) {
        return;
    }

    framebuffer_count = framebuffer_response->framebuffer_count;
    framebuffers = kmalloc(sizeof(struct framebuffer_info) * framebuffer_count);
    if (unlikely(framebuffers == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for framebuffer information");
    }

    for (size_t i = 0; i < framebuffer_count; i++) {
        struct framebuffer_info* framebuffer = &framebuffers[i];
        struct limine_framebuffer* limine_framebuffer = framebuffer_response->framebuffers[i];

        klog("[fb] found framebuffer #%zu with mode %ux%ux%u at 0x%lx\n",
                i, limine_framebuffer->width, limine_framebuffer->height,
                limine_framebuffer->bpp, limine_framebuffer->address);

        framebuffer->address = (uintptr_t) limine_framebuffer->address;

        struct fb_fix_screeninfo* fix_info = &framebuffer->fix_info;
        strncpy(fix_info->id, "LIMINE FB", sizeof(fix_info->id) - 1);
        fix_info->smem_len = limine_framebuffer->pitch * limine_framebuffer->height;
        fix_info->type = 0;
        fix_info->visual = 2;
        fix_info->line_length = limine_framebuffer->pitch;
        fix_info->mmio_len = limine_framebuffer->pitch * limine_framebuffer->height;

        struct fb_var_screeninfo* var_info = &framebuffer->var_info;
        var_info->xres = limine_framebuffer->width;
        var_info->yres = limine_framebuffer->height;
        var_info->xres_virtual = limine_framebuffer->width;
        var_info->yres_virtual = limine_framebuffer->height;
        var_info->bits_per_pixel = limine_framebuffer->bpp;
        var_info->red.msb_right = 1;
        var_info->red.offset = limine_framebuffer->red_mask_shift;
        var_info->red.length = limine_framebuffer->red_mask_size;
        var_info->green.msb_right = 1;
        var_info->green.offset = limine_framebuffer->green_mask_shift;
        var_info->green.length = limine_framebuffer->green_mask_size;
        var_info->blue.msb_right = 1;
        var_info->blue.offset = limine_framebuffer->blue_mask_shift;
        var_info->blue.length = limine_framebuffer->blue_mask_size;
        var_info->transp.msb_right = 1;
        var_info->height = -1;
        var_info->width = -1;

        char name[8];
        snprintf(name, sizeof(name) - 1, "fb%zu", i);

        if (unlikely(devfs_register(name, VFS_TYPE_CHARDEV, &fb_ops, makedev(FB_DEV_MAJOR, i)) < 0)) {
            kpanic(NULL, false, "failed to create framebuffer device %s", name);
        }
    }
}

void fb_dev_early_init(void) {
    struct limine_framebuffer_response* framebuffer_response = framebuffer_request.response;
    if (unlikely(framebuffer_response->framebuffer_count == 0)) {
        return;
    }

    struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[0];

    fb_context = flanterm_fb_init(
        flanterm_alloc,
        flanterm_free,
        (uint32_t*) framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch,
        framebuffer->red_mask_size, framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0, 0
    );
}
