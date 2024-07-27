#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char* program_name;

static void print_error(void) {
    fprintf(stderr, "try '%s -h' for more information\n", program_name);
}

static void print_help(void) {
    printf("Usage: %s [OPTION]... FILE...\n \
            Update the atime and mtime of each FILE to the current timestamp.\n \
            A FILE argument that does not exist is created empty, unless -c is supplied.\n \
            A FILE argument that of - is causes touch to change the times of the file associated with stdout.\n\n \
            -c        do not create any files\n \
            -h        display this help and exit\n", program_name);
}

int main(int argc, char** argv) {
    program_name = argv[0];

    bool no_create = false;

    int c;
    while ((c = getopt(argc, argv, "ch")) != -1) {
        switch (c) {
            case 'c':
                no_create = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        print_error();
        return EXIT_FAILURE;
    }

    for (int i = optind; i < argc; i++) {
        int fd;
        if (!strcmp(argv[i], "-")) {
            fd = STDOUT_FILENO;
        } else {
            fd = open(argv[i], O_PATH | (no_create ? O_RDONLY : O_WRONLY | O_CREAT));
        }

        if (fd < 0) {
            perror("open");
            return EXIT_FAILURE;
        }

        close(fd);
    }

    return EXIT_SUCCESS;
}
