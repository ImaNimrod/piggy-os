#include <cpu/asm.h>
#include <dev/serial.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/string.h>

static spinlock_t print_lock = {0};

static int itoa(long value, unsigned int radix, bool is_uppercase, bool is_unsigned, char* buffer) {
	char *pbuffer = buffer;
	bool negative = false;

    if (radix > 16) {
        return 0;
    }

	if (value < 0 && !is_unsigned) {
		negative = true;
		value = -value;
	}

	do {
		int digit = value % radix;
		*(pbuffer++) = (digit < 10 ? '0' + digit : (is_uppercase ? 'A' : 'a') + digit - 10);
		value /= radix;
	} while (value > 0);

    if (negative) {
        *(pbuffer++) = '-';
    }

	*(pbuffer) = '\0';

	int len = (pbuffer - buffer);
	for (int i = 0; i < len / 2; i++) {
		char j = buffer[i];
        int index = len - i - 1;
		buffer[i] = buffer[index];
		buffer[index] = j;
	}

	return len;
}

static int pad(char* ptr, int len, char pad_char, int pad_to, char* buffer) {
	char* pbuffer = buffer;
	if (pad_to == 0) {
        pad_to = len;
    }

    bool overflow = false;
    if (len > pad_to) {
        len = pad_to;
        overflow = true;
    }

    int i;
	for (i = pad_to - len; i > 0; i--) {
		*(pbuffer++) = pad_char;
	}

	for(i = len; i > 0; i --) {
		*(pbuffer++) = *(ptr++);
	}

	len = pbuffer - buffer;
	if (overflow) {
		for (i = 0; i < 3 && pbuffer > buffer; i ++) {
			*(pbuffer-- - 1) = '*';
		}
	}

	return len;
}

static int puts(char* str, int len) {
    int i;
    for (i = 0; i < len; i++) {
        serial_putc(COM1, str[i]);
    }

    return i;
}

// TODO: weird shit is happening; i need to just steal this
static int klog_internal(const char* str, va_list args) {
    char buf[24];
    char buf2[24];
    char c;
    int n;

    while ((c = *(str++))) {
        int len;

        if (c != '%') {
            len = 1;
            serial_putc(COM1, c);
            continue;
        } else {
            char pad_char = ' ';
            int pad_to = 0;
            bool is_long = false;
            char* ptr;

            c = *(str++);

            if (c == '0') {
                pad_char = '0';
            }

            while (c >= '0' && c <= '9') {
                pad_to = pad_to * 10 + (c - '0');
                c = *(str++);
            }

            if (pad_to > (int) sizeof(buf)) {
                pad_to = sizeof(buf);
            }

            if (c == 'l') {
                is_long = true;
                c = *(str++);
            }

            switch (c) {
                case '\0':
                    return n;
                case 'u':
                case 'd':
                    if (is_long) {
                        len = itoa(va_arg(args, unsigned long), 10, false, (c == 'u'), buf2);
                    } else {
                        if (c == 'u') {
                            len = itoa((unsigned long) va_arg(args, unsigned int), 10, false, true, buf2);
                        } else {
                            len = itoa((long) va_arg(args, int), 10, false, false, buf2);
                        }
                    }

                    len = pad(buf2, len, pad_char, pad_to, buf);
                    len = puts(buf, len);
                    break;
                case 'c':
                    serial_putc(COM1, (char) va_arg(args, int));
                    break;
                case 's':
                    ptr = (char*) va_arg(args, char*);
                    len = strlen(ptr);

                    if (pad_to > 0) {
                        pad(ptr, len, pad_char, pad_to, buf);
                        len = puts(buf, len);
                    } else {
                        len = puts(ptr, len);
                    }

                    break;
                case 'x':
                case 'X':
                    if (is_long) {
                        len = itoa(va_arg(args, unsigned long), 16, (c == 'X'), true, buf2);
                    } else {
                        len = itoa((unsigned long) va_arg(args, unsigned int), 16, (c == 'X'), true, buf2);
                    }

                    len = pad(buf2, len, pad_char, pad_to, buf);
                    len = puts(buf, len);
                    break;
                case 'o':
                    if (is_long) {
                        len = itoa(va_arg(args, unsigned long), 8, false, true, buf);
                    } else {
                        len = itoa((unsigned long) va_arg(args, unsigned int), 8, false, true, buf);
                    }

                    len = pad(buf2, len, pad_char, pad_to, buf);
                    len = puts(buf, len);
                    break;
                default:
                    len = 1;
                    serial_putc(COM1, c);
                    break;
            }

            memset(buf, 0, sizeof(buf));
            memset(buf2, 0, sizeof(buf2));
            n += len;
        }
    }

    return n;
}

void klog(const char* fmt, ...) {
    if (!fmt) {
        return;
    }

    spinlock_acquire(&print_lock);

    va_list args;
    va_start(args, fmt);
    klog_internal(fmt, args);
    va_end(args);

    spinlock_release(&print_lock);
}

__attribute__((noreturn)) void kpanic(struct registers* r, const char* fmt, ...) {
    cli();

    spinlock_release(&print_lock);

    klog("\n\n====== KERNEL PANIC ======\nKernel panicked due to reason: ");

    va_list args;
    va_start(args, fmt);
    klog_internal(fmt, args);
    va_end(args);

    if (r != NULL) {
        klog("\n==========================\n\n");
        klog("RIP: 0x%08x%08x RFLAGS: 0x%08x\n", r->rip >> 32, r->rip, r->rflags);
        klog("RSP: 0x%08x%08x RBP: 0x%08x%08x\n", r->rsp >> 32, r->rsp, r->rbp >> 32, r->rbp);
        klog("CS: 0x%04x SS: 0x%04x CR3: 0x%08x%08x\n", r->cs, r->ss, read_cr3() >> 32, read_cr3());
        klog("ERROR CODE: 0x%08x", r->error_code);
    }

    klog_internal("\n==========================\n", NULL);

    for (;;) {
        hlt();
    }
}
