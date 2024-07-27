#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CAT_BUF_SIZE 1024

static char* program_name;

static void cat(int fd) {
    char* buf = malloc(CAT_BUF_SIZE * sizeof(char));
    if (!buf) {
        perror(program_name);
        exit(EXIT_FAILURE);
    }

    ssize_t n;
    while ((n = read(fd, buf, CAT_BUF_SIZE)) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            perror(program_name);
            exit(EXIT_FAILURE);
        }
    }

    if (n < 0) {
        perror(program_name);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) {
    program_name = argv[0];

    if (argc <= 1) {
        cat(STDIN_FILENO);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd;
            if (!strcmp(argv[i], "-")) {
                fd = STDIN_FILENO;
            } else {
                fd = open(argv[i], O_RDONLY);
            }

            if (fd < 0) {
                perror(program_name);
                return EXIT_FAILURE;
            }

            cat(fd);
            close(fd);
        }
    }

    return EXIT_SUCCESS;
}
