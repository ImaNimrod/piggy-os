#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: hostname [HOSTNAME]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    char buf[HOST_NAME_MAX];

    if (argc == 1) {
        size_t len = strlen(argv[0]);
        if (len >= HOST_NAME_MAX) {
            errx(EXIT_FAILURE, "hostname exceeds maximum length of %u", HOST_NAME_MAX);
        }

        strncpy(buf, argv[0], sizeof(buf));
        buf[HOST_NAME_MAX - 1] = '\0';

        if (sethostname(buf, len) < 0) {
            err(EXIT_FAILURE, "sethostname");
        }
    } else {
        if (gethostname(buf, sizeof(buf)) < 0) {
            err(EXIT_FAILURE, "gethostname");
        }

        buf[HOST_NAME_MAX - 1] = '\0';

        puts(buf);
    }

    return EXIT_SUCCESS;
}
