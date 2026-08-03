#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: tty [-s]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool silent = false;

    int c;
    while ((c = getopt(argc, argv, "s")) != -1) {
        switch (c) {
            case 's':
                silent = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc >= 1) {
        warnx("extra operands provided");
        usage();
    }

    const char* name = ttyname(STDIN_FILENO);
    if (!name && errno != ENOTTY) {
        err(EXIT_FAILURE, "ttyname");
    }

    if (!silent) {
        puts(name ? name : "not a tty");
    }

    return name ? EXIT_SUCCESS : EXIT_FAILURE;
}
