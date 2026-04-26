#include <dev/char/fb.h>
#include <dev/char/tty.h>
#include <errno.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <types.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h> 

#include "../../utils/flanterm/src/flanterm.h"

#define CTRL(c) ((c) & 0x1f)

#define INPUT_BUF_SIZE 1024

#define IGNBRK  0x00001
#define BRKINT  0x00002
#define IGNPAR  0x00004
#define PARMRK  0x00008
#define INPCK   0x00010
#define ISTRIP  0x00020
#define INLCR   0x00040
#define IGNCR   0x00080
#define ICRNL   0x00100
#define IUCLC   0x00200
#define IXON    0x00400
#define IXANY   0x00800
#define IXOFF   0x01000
#define IMAXBEL 0x02000
#define IUTF8   0x04000

#define OPOST   0x00001
#define OLCUC   0x00002
#define ONLCR   0x00004
#define OCRNL   0x00008
#define ONOCR   0x00010
#define ONLRET  0x00020
#define OFILL   0x00040

#define ISIG    0x00001
#define ICANON  0x00002
#define ECHO    0x00004
#define ECHOE   0x00008
#define ECHOK   0x00010
#define ECHONL  0x00020
#define NOFLSH  0x00040
#define TOSTOP  0x00080
#define ECHOCTL 0x00100
#define ECHOPRT 0x00200
#define ECHOKE  0x00400
#define IEXTEN  0x00800

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
#define CBAUD  0010017

#define NCCS 32

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

bool tty_is_ready;

static ssize_t tty_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);
static ssize_t tty_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);
static int tty_ioctl(dev_t dev, int request, void* argp);

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

static const char crnl[2] = { '\r', '\n' };

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

static ssize_t tty_read(dev_t dev, void* buf, size_t count, off_t offset, int flags) {
    (void) dev;
    (void) offset;
    (void) flags;

    while (!input_buf_flushed) {
        scheduler_yield(true);
    }

    if (input_buf_index == 0) {
        if (termios.c_lflag & ICANON) {
            input_buf_flushed = false;
            return 0;
        }
        scheduler_yield(true);
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

    ssize_t ret;
    if ((ret = USER_MEMCPY_MAYBE_TO_USER(buf, input_buf, to_copy)) < 0) {
        spinlock_release(&read_lock);
        return ret;
    }

    memmove(input_buf, input_buf + to_copy, input_buf_index - to_copy);
    input_buf_index -= to_copy;
    if (input_buf_index == 0) {
        input_buf_flushed = false;
    }

    spinlock_release(&read_lock);
    return to_copy;
}

static ssize_t tty_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) dev;
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
            internal_write(crnl, sizeof(crnl));
        } else {
            internal_write(&c, 1);
        }
    }

    return count;
}

static int tty_ioctl(dev_t dev, int request, void* argp) {
    (void) dev;

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
    }

    if ((termios.c_iflag & INLCR) && c == '\n') {
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
            if (c == '\n') {
                force_echo = (termios.c_lflag & ECHONL);
            }
        }
    }

    if (should_append) {
        if (input_buf_index >= INPUT_BUF_SIZE) {
            goto end;
        }

        input_buf[input_buf_index++] = c;
    }

    if (should_append && (force_echo || (termios.c_lflag & ECHO))) {
        if ((c < 32 && c != '\n' && c != '\t') || c == 127) {
            char control_char[2];
            control_char[0] = '^';
            control_char[1] = (c == 127) ? '?' : (c ^ 0x40);
            internal_write(control_char, 2);
        } else {
            if (c == '\n' && (termios.c_oflag & ONLCR)) {
                internal_write(crnl, sizeof(crnl));
            } else {
                internal_write(&c, sizeof(c));
            }
        }
    }

    if (should_flush) {
        input_buf_flushed = true;
    }

end:
    spinlock_release(&read_lock);
}

void tty_init(void) {
    if (unlikely(devfs_register("tty", VFS_TYPE_CHARDEV, &tty_ops, makedev(TTY_DEV_MAJOR, 0)) < 0)) {
        kpanic(NULL, false, "failed to create tty device");
    }

    input_buf = kmalloc(INPUT_BUF_SIZE * sizeof(char));
    if (unlikely(input_buf == NULL)) {
        kpanic(NULL, false, "failed to create tty input buffer");
    }

    termios.c_iflag = ICRNL | IXON;
    termios.c_oflag = OPOST | ONLCR;
    termios.c_cflag = CREAD | CS8;
    termios.c_lflag = ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;

    termios.c_cc[VEOF] = CTRL('D');
    termios.c_cc[VERASE] = '\b';
    termios.c_cc[VINTR] = CTRL('C');
    termios.c_cc[VKILL] = CTRL('U');
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VQUIT] = CTRL('\\');
    termios.c_cc[VSTART] = CTRL('Q');
    termios.c_cc[VSTOP] = CTRL('S');
    termios.c_cc[VSUSP] = CTRL('Z');


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
