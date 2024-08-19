#ifndef _KERNEL_DEV_CHAR_TTY_H
#define _KERNEL_DEV_CHAR_TTY_H

#include <stddef.h>
#include <types.h>
#include <utils/ringbuf.h>

#define TTY_MAJ 2

#define IOCTL_TTYFLUSH      0x3500
#define IOCTL_TTYGETATTR    0x3501
#define IOCTL_TTYSETATTR    0x3502

struct tty {
    struct termios attr;
    ringbuf_t* input_buf;
    ringbuf_t* canon_buf;
    void* private;
};

extern struct tty* active_tty;

void tty_init(void);

#endif /* _KERNEL_DEV_CHAR_TTY_H */
