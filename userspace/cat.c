#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CAT_BUF_SIZE 1024

static void cat(int fd) {
    char buf[CAT_BUF_SIZE];

    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            exit(EXIT_FAILURE);
        }
    }

    if (n < 0) {
        exit(EXIT_FAILURE);
    }
}

int main(int argc, const char* argv[]) {
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
                return EXIT_FAILURE;
            }

            cat(fd);
            close(fd);
        }
    }

    return EXIT_SUCCESS;
}
