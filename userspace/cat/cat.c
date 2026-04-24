#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE (1024 * 8)

static char* buf;

static int do_cat(const char* filename, int fd) {
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
    fprintf(stderr, "usage: cat [FILE]...\n");
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

    char* filename;
    int fd;

    int i = 0;
    while (argv[i] != NULL || i == 0) {
        if (argv[i] == NULL || strcmp(argv[i], "-") == 0) {
            filename = "stdin";
            fd = STDIN_FILENO;
        } else {
            filename = argv[i];
            fd = open(filename, O_RDONLY);
        }

        if (fd < 0) {
            warn(filename);
            ret = EXIT_FAILURE;
        } else {
            ret |= do_cat(filename, fd);

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (argv[i] == NULL) {
            break;
        }

        i++;
    }

    free(buf);
    return ret;
}
