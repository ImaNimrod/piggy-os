#include <cpu/asm.h>
#include <dev/serial.h>
#include <utils/log.h>

static int itoa(long value, unsigned int radix, bool positive, char* buffer) {
    char* buf_ptr = buffer;
    bool negative = false;

    if (radix > 16) {
        return 0;
    }

    if (value < 0 && !positive) {
        negative = true;
        value = -value;
    }

    do {
        int digit = value % radix;
        *(buf_ptr++) = (digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= radix;
    } while (value > 0);

    if (negative) {
        *(buf_ptr++) = '-';
    }

    *(buf_ptr) = '\0';

    size_t len = (buf_ptr - buffer);

    for (size_t i = 0; i < len / 2; i++) {
        char j = buffer[i];
        buffer[i] = buffer[len-i-1];
        buffer[len-i-1] = j;
    }

    return len;
}

static void klog_internal(const char* str, va_list args) {
    char buf[24] = {0};

    while (*str != '\0') {
        if (*str != '%') {
            serial_putc(COM1, *str);
            str++;
            continue;
        }

        str++;

        bool is_long = false;
        char* ptr;

        if (*str == 'l') {
            is_long = true;
            str++;
        }

        switch (*str) {
            case '\0':
                return;
            case 'u':
            case 'd':
                if (is_long) {
                    itoa(va_arg(args, unsigned long), 10, (*str == 'u'), buf);
                } else {
                    if (*str == 'u') {
                        itoa((unsigned long) va_arg(args, unsigned int), 10, true, buf);
                    } else {
                        itoa((long) va_arg(args, int), 10, false, buf);
                    }
                }

                serial_puts(COM1, buf);
                break;
            case 'c':
                serial_putc(COM1, (char) va_arg(args, int));
                break;
            case 's':
                ptr = (char*) va_arg(args, char*);
                serial_puts(COM1, ptr);
                break;
            case 'x':
            case 'o':
                if (is_long) {
                    itoa(va_arg(args, unsigned long), (*str == 'x' ? 16 : 8), true, buf);
                } else {
                    itoa((unsigned long) va_arg(args, unsigned int), (*str == 'x' ? 16 : 8), true, buf);
                }

                serial_puts(COM1, buf);
                break;
            case '%':
                serial_putc(COM1, '%');
                str++;
                continue;
            default:
                break;
        }

        str++;
    }
}

void klog(const char* fmt, ...) {
    if (!fmt) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    klog_internal(fmt, args);
    va_end(args);
}

__attribute__((noreturn)) void kpanic(const char* fmt, ...) {
    __asm__ volatile("cli");

    klog("\n\n====== KERNEL PANIC ======\nKernel panicked due to reason: ");

    va_list args;
    va_start(args, fmt);
    klog_internal(fmt, args);
    va_end(args);

    klog("\n==========================\n");

    for (;;) {
        hlt();
    }
}
