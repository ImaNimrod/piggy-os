#include <cpu/asm.h>
#include <stdbool.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/string.h>

spinlock_t print_lock = {0};

static void print_number(int64_t number, size_t radix, char padding, int pad_length, bool is_unsigned) {
    if (radix > 16) {
        return;
    }

    int arr[50];
    int count = 0;

    if (number < 0 && !is_unsigned) {
        outb(0xe9, '-');
        number = -number;
    }

    do {
        arr[count++] = number % radix;
        number /= radix;
    } while(number);

    int i;

    if (pad_length > count) {
        for (i = 0; i < pad_length - count; i++) {
            outb(0xe9, padding);
        }
    }

    for (i = count - 1; i > -1; i--) {
        outb(0xe9, (arr[i] < 10 ? '0' + arr[i] : 'a' + arr[i] - 10));
    }
}

static void puts(const char* str) {
    while (*str != '\0') {
        outb(0xe9, *str);
        str++;
    }
}

void vklog(const char* fmt, va_list args) {
    const char* str;

    spinlock_acquire(&print_lock);

    char c;
    while ((c = *(fmt++)) != '\0') {
        if (c != '%') {
            outb(0xe9, c);
        } else {
            char padding = ' ';
            int pad_to = 0;

            c = *(fmt++);

            if (c == '0') {
                padding = '0';
            }

            while (c >= '0' && c <= '9') {
                pad_to = pad_to * 10 + (c - '0');
                c = *(fmt++);
            }

            switch (c) {
                case '\0':
                    return;
                case '%':
                    outb(0xe9, '%');
                    break;
                case 'd':
                case 'u':
                    print_number(va_arg(args, int64_t), 10, padding, pad_to, c == 'd' ? false : true);
                    break;
                case 'x':
                case 'o':
                    print_number(va_arg(args, int64_t), c == 'x' ? 16 : 8, padding, pad_to, true);
                    break;
                case 'c':
                    outb(0xe9, (char) va_arg(args, int));
                    break;
                case 's':
                    str = va_arg(args, const char*);
                    if (!str) {
                        puts("(null)");
                        break;
                    }
                    puts(str);
                    break;
                default:
                    outb(0xe9, c);
                    break;
            }
        }
    }

    spinlock_release(&print_lock);
}

void klog(const char* fmt, ...) {
    if (!cmdline_early_get_klog() || fmt == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    vklog(fmt, args);

    va_end(args);
}
