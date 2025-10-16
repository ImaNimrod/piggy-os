#include <dev/char/fb.h>
#include <dev/char/tty.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <types.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h> 

#include "../../utils/flanterm/src/flanterm.h"

#define INPUT_BUF_SIZE 2048

#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VTIME     5
#define VMIN      6
#define VSWTC     7
#define VSTART    8
#define VSTOP     9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

#define OPOST  0000001
#define OLCUC  0000002
#define ONLCR  0000004
#define OCRNL  0000010
#define ONOCR  0000020
#define ONLRET 0000040
#define OFILL  0000100
#define OFDEL  0000200

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ECHOK  0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000400
#define ECHOCTL 0001000
#define ECHOPRT 0002000
#define ECHOKE 0004000
#define IEXTEN 0100000

#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017

#define CSIZE  0000060
#define CS5    0000000
#define CS6    0000020
#define CS7    0000040
#define CS8    0000060
#define CSTOPB 0000100
#define CREAD  0000200
#define PARENB 0000400
#define PARODD 0001000
#define HUPCL  0002000
#define CLOCAL 0004000

bool tty_is_ready;

static ssize_t tty_read(int minor, void* buf, size_t count, off_t offset, int flags);
static ssize_t tty_write(int minor, const void* buf, size_t count, off_t offset, int flags);
static int tty_ioctl(int minor, int request, void* argp);

static struct device_ops tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
};

static struct termios termios;
static struct winsize winsize;

static char* input_buf;
static bool input_buf_flushed;
static size_t input_buf_index;

static spinlock_t read_lock;
static spinlock_t write_lock;

static void internal_write(const char* buf, size_t count);

static inline void do_backspace(void) {
    if (input_buf_index == 0) {
        return;
    }

    char last = input_buf[input_buf_index - 1];

    if (last < 32 || last == 127) {
        char caret_backspace[6] = { '\b', '\b', ' ', ' ', '\b', '\b' };
        internal_write(caret_backspace, sizeof(caret_backspace));
    } else {
        char normal_backspace[3] = { '\b', ' ', '\b' };
        internal_write(normal_backspace, sizeof(normal_backspace));
    }

    input_buf_index--;
}

static void internal_write(const char* buf, size_t count) {
    spinlock_acquire(&write_lock);
    flanterm_write(fb_context, buf, count);
    spinlock_release(&write_lock);
}

static ssize_t tty_read(int minor, void* buf, size_t count, off_t offset, int flags) {
    (void) minor;
    (void) offset;
    (void) flags;

    while (!input_buf_flushed) {
        scheduler_yield(true);
    }

    if (input_buf_index == 0) {
        input_buf_flushed = false;
        return 0;
    }

    spinlock_acquire(&read_lock);

    size_t max_to_copy = MIN(count, input_buf_index);
    size_t to_copy = max_to_copy;
    if (termios.c_lflag & ICANON) {
        for (to_copy = 1; to_copy < max_to_copy; to_copy++) {
            if (input_buf[to_copy - 1] == '\n') {
                break;
            }
        }
    }

    USER_MEMCPY_MAYBE_TO_USER(buf, input_buf, to_copy);

    memmove(input_buf, input_buf + to_copy, input_buf_index - to_copy);
    input_buf_index -= to_copy;
    if (input_buf_index == 0) {
        input_buf_flushed = false;
    }

    spinlock_release(&read_lock);
    return to_copy;
}

static ssize_t tty_write(int minor, const void* buf, size_t count, off_t offset, int flags) {
    (void) minor;
    (void) offset;
    (void) flags;

    const char* cbuf = buf;
    ssize_t ret;

    for (size_t i = 0; i < count; i++) {
        char c;
        if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&c, &cbuf[i], sizeof(char))) < 0) {
            return ret;
        }

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
            ret = user_memcpy_to_user(argp, (const void*) &winsize, sizeof(struct winsize));
            break;
        case TIOCSWINSZ:
            break;
        default:
            ret = -ENOTTY;
            break;
    }

    return ret;
}

void tty_add_char(char c) {
    spinlock_acquire(&read_lock);

    if (termios.c_iflag & ISTRIP) {
        c &= 0x7f;
    }

    if ((termios.c_iflag & IGNCR) && c == '\r') {
        goto end;
    }

    if ((termios.c_iflag & ICRNL) && c == '\r') {
        c = '\n';
    } else if ((termios.c_iflag & INLCR) && c == '\n') {
        c = '\r';
    }

    bool force_echo = false;
    bool should_append = true;
    bool should_flush = false;

    if (!(termios.c_lflag & ICANON)) {
        should_flush = true;
    } else {
        if (c == termios.c_cc[VERASE] && (termios.c_lflag & ECHOE)) {
            do_backspace();
            goto end;
        }

        if (c == termios.c_cc[VKILL] && (termios.c_lflag & ECHOK)) {
            while (input_buf_index > 0 && input_buf[input_buf_index - 1] != '\n') {
                do_backspace();
            }
            goto end;
        }

        if (c == termios.c_cc[VEOF]) {
            should_append = false;
            should_flush = true;
        }

        if (c == '\n' || c == '\r' || c == termios.c_cc[VEOL]) {
            should_flush = true;
            force_echo = !!(termios.c_lflag & ECHONL);
        }
    }

    if (should_append) {
        if (input_buf_index >= INPUT_BUF_SIZE) {
            goto end;
        }

        input_buf[input_buf_index++] = c;
    }

    if (should_append && (force_echo || (termios.c_lflag & ECHO))) {
        if ((c <= 31 || c == 127) && c != '\n') {
            char control_char[2];
            control_char[0] = '^';

            if (c <= 26 && c != 10) {
                control_char[1] = 'A' + c - 1;
            } else if (c == 27) {
                control_char[1] = '[';
            } else if (c == 28) {
                control_char[1] = '\\';
            } else if (c == 29) {
                control_char[1] = ']';
            } else if (c == 30) {
                control_char[1] = '^';
            } else if (c == 31) {
                control_char[1] = '_';
            } else if (c == 127) {
                control_char[1] = '?';
            }

            internal_write(control_char, sizeof(control_char));
        } else {
            internal_write(&c, sizeof(c));
        }

        if (should_flush) {
            input_buf_flushed = true;
        }
    }

end:
    spinlock_release(&read_lock);
}

void tty_init(void) {
    if (unlikely(devfs_register_device("tty", VFS_TYPE_CHARDEV, &tty_ops, makedev(TTY_DEV_MAJOR, TTY_DEV_MINOR)) < 0)) {
        kpanic(NULL, false, "failed to create tty device");
    }

    input_buf = kmalloc(INPUT_BUF_SIZE * sizeof(char));
    if (unlikely(input_buf == NULL)) {
        kpanic(NULL, false, "failed to create tty input buffer");
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
