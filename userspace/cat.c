#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE (1024 * 8)

static char* buf;

static int do_cat(char* filename, int rfd) {
    if (buf == NULL) {
        if ((buf = malloc(BUF_SIZE)) == NULL) {
            err(EXIT_FAILURE, "malloc");
        }
    }

    ssize_t nread;
    while ((nread = read(rfd, buf, BUF_SIZE)) > 0) {
        if (write(STDOUT_FILENO, buf, nread) != nread) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: cat [FILE]...\n"
            "With no FILE, or when FILE is -, read from stdin.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    int ret = EXIT_SUCCESS;

    int fd;
    char* path;

    int i = 0;
    while ((path = argv[i]) || i == 0) {
        if (path == NULL || !strcmp(path, "-")) {
            fd = STDIN_FILENO;
            path = "stdin";
        } else {
            fd = open(path, O_RDONLY);
        }

        if (fd < 0) {
            warn("%s", path);
            ret = EXIT_FAILURE;
        } else {
            ret = do_cat(path, fd);

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (path == NULL) {
            break;
        }

        i++;
    }

    if (buf != NULL) {
        free(buf);
    }

    return ret;
}
