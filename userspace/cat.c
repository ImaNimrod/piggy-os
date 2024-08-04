#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRAM_NAME "cat"

#define CAT_BUF_SIZE 1024

static char buf[CAT_BUF_SIZE];

static int cat(int fd) {
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            perror(PROGRAM_NAME);
            return EXIT_FAILURE;
        }
    }

    if (n < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    int ret = EXIT_SUCCESS;

    if (argc <= 1) {
        ret = cat(STDIN_FILENO);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd;
            if (!strcmp(argv[i], "-")) {
                fd = STDIN_FILENO;
            } else {
                fd = open(argv[i], O_RDONLY);
                if (fd < 0) {
                    fputs(PROGRAM_NAME ": ", stderr);
                    perror(argv[i]);
                    ret = EXIT_FAILURE;
                    continue;
                }

                struct stat s;
                if (fstat(fd, &s) < 0) {
                    fprintf(stderr, PROGRAM_NAME ": cannot stat '%s': %s\n", argv[i], strerror(errno));
                    ret = EXIT_FAILURE;

                    close(fd);
                    continue;
                }

                if (S_ISDIR(s.st_mode)) {
                    errno = EISDIR;
                    fputs(PROGRAM_NAME ": ", stderr);
                    perror(argv[i]);
                    ret = EXIT_FAILURE;

                    close(fd);
                    continue;
                }
            }

            ret = cat(fd);
            close(fd);
        }
    }

    return ret;
}
