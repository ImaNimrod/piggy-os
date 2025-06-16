#include <dev/fbdev.h>
#include <limine.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <stddef.h>
#include <utils/log.h>
#include <utils/macros.h>

#include "../utils/flanterm/src/flanterm_backends/fb.h"

extern struct limine_framebuffer_request framebuffer_request;

struct flanterm_context* fb_context = NULL;

static void* flanterm_alloc(size_t size) {
    return (void*) (pmm_alloc(DIV_CEIL(size, PAGE_SIZE)) + HIGH_VMA);
}

static void flanterm_free(void* ptr, size_t size) {
    pmm_free((uintptr_t) ptr - HIGH_VMA, DIV_CEIL(size, PAGE_SIZE));
}

void fbdev_init(void) {
    struct limine_framebuffer_response* framebuffer_response = framebuffer_request.response;
    if (unlikely(framebuffer_response->framebuffer_count == 0)) {
        return;
    }

    struct limine_framebuffer* framebuffer = framebuffer_response->framebuffers[0];

    klog("[fbdev] using framebuffer with mode %ux%ux%u at 0x%lx\n",
         framebuffer->width, framebuffer->height, framebuffer->bpp, framebuffer->address);

    fb_context = flanterm_fb_init(
        flanterm_alloc, flanterm_free,
        (uint32_t*) framebuffer->address,
        framebuffer->width, framebuffer->height, framebuffer->pitch,
        framebuffer->red_mask_size, framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
        NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL, 0, 0, 1,
        0, 0,
        0);
}
