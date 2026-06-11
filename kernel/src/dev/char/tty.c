#include <cpu/smp.h>
#include <dev/char/fb.h>
#include <dev/char/tty.h>
#include <errno.h>
#include <flanterm.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mem/slab.h>
#include <sys/scheduler.h>
#include <sys/signal.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h> 

#define CTRL(c) ((c) & 0x1f)

#define INPUT_BUF_SIZE 1024

#define BRKINT  0x0001
#define ICRNL   0x0002
#define IGNBRK  0x0004
#define IGNCR   0x0008
#define IGNPAR  0x0010
#define INLCR   0x0020
#define INPCK   0x0040
#define ISTRIP  0x0080
#define IXANY   0x0100
#define IXOFF   0x0200
#define IXON    0x0400
#define PARMRK  0x0800

#define OPOST   0x0001
#define ONLCR   0x0002
#define OCRNL   0x0004
#define ONOCR   0x0008
#define ONLRET  0x0010
#define OFILL   0x0020
#define OFDEL   0x0040

#define B0       0
#define B50      1
#define B75      2
#define B110     3
#define B134     4
#define B150     5
#define B200     6
#define B300     7
#define B600     8
#define B1200    9
#define B1800    10
#define B2400    11
#define B4800    12
#define B9600    13
#define B19200   14
#define B38400   15
#define B57600   0010001
#define B115200  0010002
#define B230400  0010003
#define B460800  0010004
#define B500000  0010005
#define B576000  0010006
#define B921600  0010007
#define B1000000 0010010
#define B1152000 0010011
#define B1500000 0010012
#define B2000000 0010013
#define B2500000 0010014
#define B3000000 0010015
#define B3500000 0010016
#define B4000000 0010017

#define CBAUD   0x100f
#define CLOCAL  0x0010
#define CREAD   0x0020
#define CSIZE   0x00c0
#define CS5     0x0000
#define CS6     0x0040
#define CS7     0x0080
#define CS8     0x00c0
#define CSTOPB  0x0100
#define HUPCL   0x0200
#define PARENB  0x0400
#define PARODD  0x0800

#define ECHO    0x0001
#define ECHOE   0x0002
#define ECHOK   0x0004
#define ECHONL  0x0008
#define ICANON  0x0010
#define IEXTEN  0x0020
#define ISIG    0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100

#define VEOF    0
#define VEOL    1
#define VERASE  2
#define VINTR   3
#define VKILL   4
#define VMIN    5
#define VQUIT   6
#define VSUSP   7
#define VTIME   8
#define VSTART  9
#define VSTOP   10

bool tty_is_ready;

static ssize_t tty_read(dev_t dev, void* buf, size_t count, off_t offset, int flags);
static ssize_t tty_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);
static int tty_ioctl(dev_t dev, int request, void* argp);

static struct device_ops tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
};

static pid_t foreground_pgid;
static struct termios termios;
static struct winsize winsize;

static char* input_buf;
static bool input_buf_flushed;
static size_t input_buf_index;

static spinlock_t read_lock;
static spinlock_t write_lock;
static spinlock_t tty_lock;

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
        scheduler_yield();
    }

    if (input_buf_index == 0) {
        if (termios.c_lflag & ICANON) {
            input_buf_flushed = false;
            return 0;
        }

        scheduler_yield();
    }

    spinlock_acquire(&read_lock);

    size_t max_to_copy = MIN(count, input_buf_index);
    size_t to_copy = max_to_copy;
    if (termios.c_lflag & ICANON) {
        for (to_copy = 1; to_copy < max_to_copy; to_copy++) {
            char end = input_buf[to_copy - 1];
            if (end == '\n' || end == termios.c_cc[VEOL] || end == termios.c_cc[VEOF]) {
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

        if (termios.c_oflag & OPOST) {
            if (c == '\n' && (termios.c_oflag & ONLCR)) {
                internal_write(crnl, sizeof(crnl));
                continue;
            }

            if (c == '\r' && (termios.c_oflag & OCRNL)) {
                c = '\n';
            }
        }

        internal_write(&c, 1);
    }

    return count;
}

static int tty_ioctl(dev_t dev, int request, void* argp) {
    (void) dev;

    int ret = 0;

    spinlock_acquire(&tty_lock);

    switch (request) {
        case TCGETS:
            ret = user_memcpy_to_user(argp, (const void*) &termios, sizeof(struct termios));
            break;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            ret = user_memcpy_from_user((void*) &termios, argp, sizeof(struct termios));
            break;
        case TIOCGPGRP:
            ret = user_memcpy_to_user(argp, (const void*) &foreground_pgid, sizeof(pid_t));
            break;
        case TIOCSPGRP:
            pid_t new_foreground_pgid;
            if ((ret = user_memcpy_from_user((void*) &new_foreground_pgid, argp, sizeof(pid_t))) < 0) {
                break;
            }

            if (new_foreground_pgid <= 0) {
                ret = -EINVAL;
                break;
            }

            if (process_group_find_by_pgid(new_foreground_pgid) == NULL) {
                ret = -ESRCH;
            } else {
                foreground_pgid = new_foreground_pgid;
            }
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

    spinlock_release(&tty_lock);
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

    if (termios.c_lflag & ISIG) {
        if (c == termios.c_cc[VINTR]) {
            struct process_group* group = process_group_find_by_pgid(foreground_pgid);
            if (group != NULL) {
                signal_send_process_group(group, SIGINT);
            }

            goto end;
        }

        if (c == termios.c_cc[VQUIT]) {
            struct process_group* group = process_group_find_by_pgid(foreground_pgid);
            if (group != NULL) {
                signal_send_process_group(group, SIGQUIT);
            }

            goto end;
        }

        if (c == termios.c_cc[VSUSP]) {
            struct process_group* group = process_group_find_by_pgid(foreground_pgid);
            if (group != NULL) {
                signal_send_process_group(group, SIGTSTP);
            }

            goto end;
        }
    }

    bool force_echo = false;
    bool should_append = true;
    bool should_flush = false;

    if (!(termios.c_lflag & ICANON)) {
        should_flush = true;
    } else {
        if ((c == '\b' || c == termios.c_cc[VERASE]) && (termios.c_lflag & ECHOE)) {
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
            char control_char[2] = { '^', (c == 127) ? '?' : (c ^ 0x40) };
            internal_write(control_char, sizeof(control_char));
        } else {
            if (c == '\n' && (termios.c_lflag & ECHONL)) {
                internal_write(crnl, sizeof(crnl));
            } else {
                internal_write(&c, 1);
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

    spinlock_init(&read_lock);
    spinlock_init(&write_lock);
    spinlock_init(&tty_lock);

    termios.c_iflag = ICRNL | IXON;
    termios.c_oflag = OPOST | ONLCR;
    termios.c_cflag = B38400 | CREAD | CS8;
    termios.c_lflag = ECHO | ECHOE | ECHOK | ECHONL | ICANON | IEXTEN | ISIG;

    termios.c_cc[VEOF] = CTRL('D');
    termios.c_cc[VERASE] = CTRL('?');
    termios.c_cc[VINTR] = CTRL('C');
    termios.c_cc[VKILL] = CTRL('U');
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VQUIT] = CTRL('\\');
    termios.c_cc[VSTART] = CTRL('Q');
    termios.c_cc[VSTOP] = CTRL('S');
    termios.c_cc[VSUSP] = CTRL('Z');
    termios.c_cc[VTIME] = 0;

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
