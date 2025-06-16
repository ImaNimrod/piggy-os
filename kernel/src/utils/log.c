#include <dev/fbdev.h>
#include <dev/serial.h>
#include <stdarg.h>
#include <utils/log.h>
#include <utils/spinlock.h>

#include "flanterm/flanterm.h"
#include "printf/printf.h"

static spinlock_t print_lock = {0};

void _putchar(char c) {
    serial_putc(PORT_COM1, c);
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
