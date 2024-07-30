#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME "date"
#define STANDARD_FORMAT "%a %b %e %H:%M:%S %Y"
#define RFC5322_FORMAT "%a, %d %b %Y %H:%M:%S"

static void print_error(void) {
    fputs("try " PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("Usage: " PROGRAM_NAME " [OPTION]... [+FORMAT]\n \
            Print the time and date in the given FORMAT.\n\n \
            -R        output date and time in RFC 5322 format\n \
            -h        display this help and exit\n");
}

int main(int argc, char** argv) {
    bool rfc5322 = false;

    int c;
    while ((c = getopt(argc, argv, "hR")) != -1) {
        switch (c) {
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case 'R':
                rfc5322 = true;
                break;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind + 1 < argc) {
        fprintf(stderr, PROGRAM_NAME ": extra operand '%s'\n", argv[optind + 1]);
        print_error();
        return EXIT_FAILURE;
    }

    const char* format;

    if (optind < argc) {
        if (*argv[optind] != '+') {
            fprintf(stderr, PROGRAM_NAME ": invalid format '%s'\n", argv[optind]);
            return EXIT_FAILURE;
        }

        if (rfc5322) {
            fprintf(stderr, PROGRAM_NAME ": multiple output formats specified\n");
            return EXIT_FAILURE;
        }

        format = argv[optind] + 1;
    } else if (rfc5322) {
        format = RFC5322_FORMAT;
    } else {
        format = STANDARD_FORMAT;
    }

    time_t t = time(NULL);
    struct tm* tm = localtime(&t);

    char buffer[64];

    if (strftime(buffer, sizeof(buffer), format, tm) == 0) {
        fputs(PROGRAM_NAME ": unable to create formatted timestamp\n", stderr);
        return EXIT_FAILURE;
    } 

    puts(buffer);
    putchar('\n');
    return EXIT_SUCCESS;
}
