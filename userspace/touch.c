#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "touch"

static void print_error(void) {
    fputs("try " PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("Usage: " PROGRAM_NAME " [OPTION]... FILE...\n \
            Update the atime and mtime of each FILE to the current timestamp.\n \
            A FILE argument that does not exist is created empty, unless -c is supplied.\n \
            A FILE argument that of - is causes touch to change the times of the file associated with stdout.\n\n \
            -c        do not create any files\n \
            -h        display this help and exit\n");
}

int main(int argc, char** argv) {
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
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
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
            perror(PROGRAM_NAME);
            return EXIT_FAILURE;
        }

        close(fd);
    }

    return EXIT_SUCCESS;
}
