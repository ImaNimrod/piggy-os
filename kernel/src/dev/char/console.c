#include <dev/char/console.h>
#include <dev/char/fb.h>
#include <flanterm.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/string.h>
#include <utils/usercopy.h> 

static ssize_t console_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);
static short console_poll(dev_t dev, short events, struct poll_table* pt);

static mutex_t console_mutex;

static struct device_ops console_ops = {
    .write = console_write,
    .poll = console_poll,
};

static ssize_t console_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) dev;
    (void) offset;
    (void) flags;

    mutex_acquire(&console_mutex);

    const char* cbuf = buf;
    ssize_t ret;

    for (size_t i = 0; i < count; i++) {
        char c;
        if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&c, &cbuf[i], sizeof(char))) < 0) {
            return ret;
        }

        if (c == '\n') {
            static const char crnl[2] = { '\r', '\n' };
            flanterm_write(fb_context, crnl, sizeof(crnl));
        } else {
            flanterm_write(fb_context, &c, sizeof(c));
        }
    }

    mutex_release(&console_mutex);
    return count;
}

static short console_poll(dev_t dev, short events, struct poll_table* pt) {
    (void) dev;
    (void) pt;

    short revents = 0;

    if (events & POLLOUT) {
        revents |= POLLOUT;
    }

    return revents;
}

void console_init(void) {
    if (unlikely(devfs_register("console", VFS_TYPE_CHARDEV, &console_ops, makedev(CONSOLE_DEV_MAJOR, 0)) < 0)) {
        kpanic(NULL, false, "failed to create console device");
    }

    mutex_init(&console_mutex);
}
