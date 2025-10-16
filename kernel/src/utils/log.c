#include <dev/char/fb.h>
#include <dev/serial.h>
#include <stdarg.h>
#include <utils/log.h>
#include <utils/spinlock.h>

#include "flanterm/src/flanterm.h"
#include "printf/printf.h"

static spinlock_t print_lock;

void _putchar(char c) {
    serial_putc(COM1_PORT, c);

    if (fb_context != NULL) {
        flanterm_write(fb_context, &c, sizeof(char));
    }
}

void klog(const char* fmt, ...) {
    spinlock_acquire(&print_lock);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    spinlock_release(&print_lock);
}
