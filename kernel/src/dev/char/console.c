#include <dev/char/console.h>
#include <dev/char/fb.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/spinlock.h>
#include <utils/string.h>
#include <utils/usercopy.h> 

#include "../../utils/flanterm/src/flanterm.h"

static ssize_t console_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags);

static spinlock_t console_lock;

static struct device_ops console_ops = {
    .write = console_write,
};

static ssize_t console_write(dev_t dev, const void* buf, size_t count, off_t offset, int flags) {
    (void) dev;
    (void) offset;
    (void) flags;

    spinlock_acquire(&console_lock);

    const char* cbuf = buf;
    ssize_t ret;

    for (size_t i = 0; i < count; i++) {
        char c;
        if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&c, &cbuf[i], sizeof(char))) < 0) {
            return ret;
        }

        flanterm_write(fb_context, &c, sizeof(char));
    }

    spinlock_release(&console_lock);
    return count;
}

void console_init(void) {
    if (unlikely(devfs_register("console", VFS_TYPE_CHARDEV, &console_ops, makedev(CONSOLE_DEV_MAJOR, 0)) < 0)) {
        kpanic(NULL, false, "failed to create console device");
    }
}
