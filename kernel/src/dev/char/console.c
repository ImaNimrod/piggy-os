#include <dev/char/console.h>
#include <dev/serial.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

static ssize_t console_read(struct vfs_node* node, void* buf, off_t offset, size_t count) {
    (void) offset;

    spinlock_acquire(&node->lock);

    size_t actual_count = count;
    uint8_t* temp_buf = buf;

    while (actual_count--) {
        *temp_buf++ = serial_getc((uint16_t) (uintptr_t) node->private);
    }

    spinlock_release(&node->lock);
    return count;
}

static ssize_t console_write(struct vfs_node* node, const void* buf, off_t offset, size_t count) {
    (void) offset;

    spinlock_acquire(&node->lock);

    size_t actual_count = count;
    const uint8_t* temp_buf = buf;

    while (actual_count--) {
        serial_putc((uint16_t) ((uintptr_t) node->private), *temp_buf++);
    }

    spinlock_release(&node->lock);
    return count;
}

void console_init(void) {
    if (!serial_init(COM2)) {
        kpanic(NULL, "failed to initialize serial port for console device");
    }

    struct device console_dev = {
        .name = "console",
        .mode = S_IFCHR,
        .private = (void*) COM2,
        .read = console_read,
        .write = console_write,
    };

    if (!devfs_add_device(&console_dev)) {
        kpanic(NULL, "failed to add console device to devfs");
    }
}
