#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: sync [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    int ret = EXIT_SUCCESS;

    if (argc >= 1) {
        for (int i = 0; i < argc; i++) {
            int fd = open(argv[i], O_WRONLY);
            if (fd < 0) {
                warn(argv[i]);
                ret = EXIT_FAILURE;
                continue;
            }

            if (fsync(fd) < 0) {
                warn(argv[i]);
                ret = EXIT_FAILURE;
            }

            close(fd);
        }
    } else {
        sync();
    }

    return ret;
}
