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
    serial_init(COM2);

    struct vfs_node* console_dev = devfs_create_device("console");
    if (console_dev == NULL) {
        kpanic(NULL, "failed to create console device for devfs");
    }

    console_dev->stat = (struct stat) {
        .st_dev = makedev(0, 1),
        .st_mode = S_IFCHR,
        .st_rdev = makedev(CONSOLE_MAJ, 0),
        .st_size = 0,
        .st_blksize = 4096,
        .st_blocks = 0,
    };

    console_dev->private = (void*) COM2;

    console_dev->read = console_read;
    console_dev->write = console_write;
}
