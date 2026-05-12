#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: add support for padding, alignment, floating point formatters

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

static void print_arg(char spec, const char* arg) {
    if (arg == NULL) {
        arg = "";
    }

    switch (spec) {
        case 's':
            fputs(arg, stdout);
            break;
        case 'd':
        case 'i':
            printf("%d", atoi(arg));
            break;
        case 'u':
        case 'x':
            printf("%x", (unsigned) strtoul(arg, NULL, 10));
            break;
        case 'c':
            putchar(arg[0]);
            break;
        default:
            putchar('%');
            putchar(spec);
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

    const char* format = argv[0];

    int arg_index = 1;

    while (*format != '\0') {
        if (*format == '\\') {
            format = escape_char((char*) format + 1);
            continue;
        }

        if (*format == '%') {
            format++;

            if (*format == '%') {
                putchar('%');
                format++;
                continue;
            }

            const char* arg = (arg_index < argc) ? argv[arg_index] : "";
            arg_index++;

            print_arg(*format++, arg);
            continue;
        }

        putchar(*format++);
    }

    return EXIT_SUCCESS;
}
