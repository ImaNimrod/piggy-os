#include <dev/char/fb.h>
#include <dev/char/tty.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <types.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/usercopy.h> 

#include "../../utils/flanterm/src/flanterm.h"

#include <utils/log.h>

static ssize_t tty_read(int minor, void* buf, size_t count, off_t offset, int flags);
static ssize_t tty_write(int minor, const void* buf, size_t count, off_t offset, int flags);
static int tty_ioctl(int minor, int request, void* argp);

bool tty_is_ready = false; 

static struct device_ops tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
};

static struct termios termios = {0};
static struct winsize winsize = {0};

// static spinlock_t read_lock = {0};
static spinlock_t write_lock = {0};

static void internal_write(const char* buf, size_t count) {
    spinlock_acquire(&write_lock);
    flanterm_write(fb_context, buf, count);
    spinlock_release(&write_lock);
}

static ssize_t tty_read(int minor, void* buf, size_t count, off_t offset, int flags) {
    (void) minor;
    (void) buf;
    (void) count;
    (void) offset;
    (void) flags;
    return -ENODEV;
}

static ssize_t tty_write(int minor, const void* buf, size_t count, off_t offset, int flags) {
    (void) minor;
    (void) offset;
    (void) flags;

    const char* cbuf = buf;

    for (size_t i = 0; i < count; i++) {
        char c = cbuf[i];

        if (c == '\n' && (termios.c_oflag & ONLCR)) {
            char cr = '\r';
            internal_write(&cr, 1);
        }

        internal_write(&c, 1);
    }

    return count;
}

static int tty_ioctl(int minor, int request, void* argp) {
    (void) minor;

    int ret = 0;

    switch (request) {
        case TCGETS:
            ret = user_memcpy_to_user(argp, (const void*) &termios, sizeof(struct termios));
            break;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            ret = user_memcpy_from_user((void*) &termios, argp, sizeof(struct termios));
            break;
        case TIOCGWINSZ:
            ret = user_memcpy_to_user(argp, (const void*) &winsize, sizeof(struct termios));
            break;
        case TIOCSWINSZ:
            break;
        default:
            ret = -ENOTTY;
            break;
    }

    return ret;
}

void tty_init(void) {
    if (unlikely(devfs_register_device("tty", VFS_TYPE_CHARDEV, &tty_ops, makedev(TTY_DEV_MAJOR, TTY_DEV_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create tty device");
    }

    termios.c_iflag = ICRNL | IXON;
    termios.c_oflag = OPOST;
    termios.c_cflag = B38400 | CS8;
    termios.c_lflag = ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;

    termios.c_cc[VMIN] = 1;
    termios.c_cc[VINTR] = 0x03;
    termios.c_cc[VQUIT] = 0x1c;
    termios.c_cc[VERASE] = '\b';
    termios.c_cc[VKILL] = 0x15;
    termios.c_cc[VEOF] = 0x04;
    termios.c_cc[VSTART] = 0x11;
    termios.c_cc[VSTOP] = 0x13;
    termios.c_cc[VSUSP] = 0x1a;

    size_t cols, rows;
    flanterm_get_dimensions(fb_context, &cols, &rows);

    winsize = (struct winsize) {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = cols * 32,
        .ws_ypixel = rows * 16,
    };

    tty_is_ready = true;
}
