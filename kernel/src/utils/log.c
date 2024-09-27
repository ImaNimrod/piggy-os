#include <cpu/asm.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/spinlock.h>

#define QEMU_DEBUG_PORT 0xe9

static spinlock_t print_lock = {0};

void _putchar(char character) {
    outb(QEMU_DEBUG_PORT, character);
}

void klog(const char* fmt, ...) {
    if (!cmdline_early_get_klog() || !fmt) {
        return;
    }

    spinlock_acquire(&print_lock);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    spinlock_release(&print_lock);
}
