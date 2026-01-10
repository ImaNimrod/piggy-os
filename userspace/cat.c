#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE (1024 * 8)

static char* buf;

static int do_cat(char* filename, int fd) {
    ssize_t nread;
    while ((nread = read(fd, buf, BUFSIZE)) > 0) {
        if (write(STDOUT_FILENO, buf, nread) != nread) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    if (nread < 0) {
        warn(filename);
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

    if ((buf = malloc(BUFSIZE)) == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    int ret = EXIT_SUCCESS;

    int fd;
    char* filename;

    int i = 0;
    char* path;
    while ((path = argv[i]) != NULL || i == 0) {
        if (path == NULL || strcmp(path, "-") == 0) {
            fd = STDIN_FILENO;
            filename = "stdin";
        } else {
            fd = open(path, O_RDONLY);
            filename = path;
        }

        if (fd < 0) {
            warn(filename);
            ret = EXIT_FAILURE;
        } else {
            ret = do_cat(filename, fd);

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (path == NULL) {
            break;
        }

        i++;
    }

    free(buf);
    return ret;
}
