#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "touch"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... FILE...\n\nUpdate the atime and mtime of each FILE to the current timestamp.\nA FILE argument that does not exist is created empty, unless -c is supplied.\nA FILE argument that of - is causes touch to change the times of the file associated with stdout.\n\n-c\tdo not create any files\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
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
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    if (optind == argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        error();
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
