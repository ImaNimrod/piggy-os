#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct format {
    bool left_align;
    bool zero_pad;

    int width;
    int precision;

    char conversion;
};

static bool build_format(char* out, size_t size, const struct format* format) {
    char* p = out;
    char* end = out + size - 1;

    if (p < end) {
        *p++ = '%';
    } else {
        return false;
    }

    if (format->left_align) {
        if (p >= end) {
            return false;
        }

        *p++ = '-';
    }

    if (format->zero_pad) {
        if (p >= end) {
            return false;
        }

        *p++ = '0';
    }

    if (format->width > 0) {
        int n = snprintf(p, end - p + 1, "%d", format->width);
        if (n < 0 || n > end - p) {
            return false;
        }

        p += n;
    }

    if (format->precision >= 0) {
        if (p >= end) {
            return false;
        }

        *p++ = '.';

        int n = snprintf(p, end - p + 1, "%d", format->precision);
        if (n < 0 || n > end - p) {
            return false;
        }

        p += n;
    }

    if (p >= end) {
        return false;
    }

    *p++ = format->conversion;
    *p = '\0';

    return true;
}

static const char* escape_char(char* str) {
    char c = '\0';

    switch (*str++) {
        case '\\':
            c = '\\';
            break;
        case 'a':
            c = '\a';
            break;
        case 'b':
            c = '\b';
            break;
        case 'f':
            c = '\f';
            break;
        case 'n':
            c = '\n';
            break;
        case 'r':
            c = '\r';
            break;
        case 't':
            c = '\t';
            break;
        case 'v':
            c = '\v';
            break;
        default:
            str--;
            if (*str < '0' || *str > '7') {
                putchar('\\');
                c = *str++;
                break;
            }

            for (size_t i = 0; i < 3; i++) {
                if (*str < '0' || *str > '7') {
                    break;
                }

                c = c * 8 + (*str - '0');
                str++;
            }

            break;
    }

    putchar(c);
    return str;
}

static const char* parse_format(const char* fmt, struct format* format) {
    format->precision = -1;

    while (*fmt == '-' || *fmt == '0') {
        switch (*fmt++) {
            case '-':
                format->left_align = true;
                break;
            case '0':
                format->zero_pad = true;
                break;
        }
    }

    while (*fmt >= '0' && *fmt <= '9') {
        format->width = format->width * 10 + (*fmt - '0');
        fmt++;
    }

    if (*fmt == '.') {
        fmt++;
        format->precision = 0;

        while (*fmt >= '0' && *fmt <= '9') {
            format->precision = format->precision * 10 + (*fmt - '0');
            fmt++;
        }
    }

    format->conversion = *fmt++;
    return fmt;
}

static void print_arg(const struct format* format, const char* arg) {
    if (arg == NULL) {
        arg = "";
    }

    char fmt[64];
    if (!build_format(fmt, sizeof(fmt), format)) {
        errx(EXIT_FAILURE, "format string too long");
    }

    switch (format->conversion) {
        case 's':
            printf(fmt, arg);
            break;
        case 'd':
        case 'i':
            printf(fmt, strtol(arg, NULL, 10));
            break;
        case 'u':
            printf(fmt, strtoul(arg, NULL, 10));
            break;
        case 'x':
            printf(fmt, strtoul(arg, NULL, 0));
            break;
        case 'c':
            printf(fmt, arg[0]);
            break;
        case 'f':
        case 'e':
        case 'g':
            printf(fmt, strtod(arg, NULL));
            break;
        default:
            putchar('%');
            putchar(format->conversion);
            break;
    }
}

static void usage(void) {
    fprintf(stderr, "usage: printf FORMAT [ARGUMENT]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        errx(EXIT_FAILURE, "missing format operand");
    }

    int arg_index = 1;
    const char* fmt = argv[0];

    while (*fmt != '\0') {
        if (*fmt == '\\') {
            fmt = escape_char((char*) fmt + 1);
            continue;
        }

        if (*fmt == '%') {
            fmt++;

            if (*fmt == '%') {
                putchar('%');
                fmt++;
                continue;
            }

            struct format format = {};
            fmt = parse_format(fmt, &format);

            const char* arg = (arg_index < argc) ? argv[arg_index++] : "";
            print_arg(&format, arg);
            continue;
        }

        putchar(*fmt++);
    }

    return EXIT_SUCCESS;
}
