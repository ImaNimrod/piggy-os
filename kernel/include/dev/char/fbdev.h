#ifndef _KERNEL_DEV_CHAR_FBDEV_H
#define _KERNEL_DEV_CHAR_FBDEV_H

#include <stddef.h>
#include <stdint.h>

#define FBDEV_MAJ 3

#define FBIOBLANK           0x1000
#define FBIOGET_FSCREENINFO 0x1001
#define FBIOGET_VSCREENINFO 0x1002

void fbdev_init(void);

#endif /* _KERNEL_DEV_CHAR_FBDEV_H */
