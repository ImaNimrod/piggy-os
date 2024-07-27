#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char* program_name;

static void print_help(void) {
    printf("Usage: %s [OPTION]... [STRING]...\n \
            Echo the STRING(s) to stdout\n\n \
            -n        do not print a trailing newline character\n\
            -h        display this help and exit", program_name);
}

int main(int argc, char** argv) {
    program_name = argv[0];

    bool do_trailing_newline = true;

    int c;
    while ((c = getopt(argc, argv, "hn"))) {
        switch (c) {
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case 'n':
                do_trailing_newline = false;
                break;
        }
    }

    for (int i = optind; i < argc; i++) {
        puts(argv[i]);
        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    if (do_trailing_newline) {
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
