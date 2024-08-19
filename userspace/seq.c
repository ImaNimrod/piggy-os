#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PROGRAM_NAME "seq"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... LAST\n   or: " PROGRAM_NAME " [OPTION]... FIRST LAST\n   or: " PROGRAM_NAME " [OPTION]... FIRST INCREMENT LAST\n\nPrint numbers from FIRST to LAST, in steps of INCREMENT.\nIf FIRST is not specified, the sequence begins at 1. If INCREMENT is not specified, the sequence increments by 1.\n\n-s STRING\tuse STRING as the seperator instead of '\\n'\n-h\t\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    char* seperator = "\n";

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
            case 's':
                seperator = optarg;
                break;
            case 'h':
                help();
                break;
            case ':':
            case '?':
                error();
                break;
        }
    }

    if (optind >= argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        error();
    }

    int num_args = argc - optind;
    if (num_args > 3) {
        fprintf(stderr, PROGRAM_NAME ": extra operand '%s'\n", argv[optind + 1]);
        error();
    }

    ssize_t end, start = 1, increment = 1;

    errno = 0;

    char* end_ptr;
    if (num_args >= 2) {
        start = strtol(argv[optind], &end_ptr, 10);
        if (errno != 0 || *end_ptr) {
            fprintf(stderr, PROGRAM_NAME ": invalid number: '%s'\n", argv[optind]);
            error();
        } 
        optind++;
    }

    if (num_args == 3) {
        increment = strtol(argv[optind], &end_ptr, 10);
        if (errno != 0 || *end_ptr) {
            fprintf(stderr, PROGRAM_NAME ": invalid number: '%s'\n", argv[optind]);
            error();
        } 
        optind++;
    }

    end = strtol(argv[optind], &end_ptr, 10);
    if (errno != 0 || *end_ptr) {
        fprintf(stderr, PROGRAM_NAME ": invalid number: '%s'\n", argv[optind]);
        error();
    } 

    if ((end < start && increment > 0) || (end > start && increment < 0)) {
        return EXIT_SUCCESS;
    }

    for (ssize_t i = start; increment > 0 ? i <= end : end <= i; i += increment) {
        if (i != start) {
            puts(seperator);
        }
        printf("%ld", i);
    }

    return EXIT_SUCCESS;
}
