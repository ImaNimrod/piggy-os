#ifndef _KERNEL_DEV_CHAR_FBDEV_H
#define _KERNEL_DEV_CHAR_FBDEV_H

#include <stddef.h>
#include <stdint.h>

#define IOCTL_FB_GETSCREENINFO 0x1001

void fbdev_init(void);

#endif /* _KERNEL_DEV_CHAR_FBDEV_H */
