#include <piggy/mount.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: mount [DEVICE] DIRECTORY:FSTYPE\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    } else if (argc > 2) {
        warnx("extra operands provided");
        usage();
    }

    char* source = NULL;
    char* target = NULL;
    if (argc == 2) {
        source = argv[0];
        target = argv[1];
    } else {
        target = argv[0];
    }

    char* delimiter = strchr(target, ':');
    if (!delimiter) {
        warnx("missing filesystem type");
        usage();
    }

    target[delimiter - target] = '\0';
    char* fstype = delimiter + 1;

    if (mount(source, target, fstype) < 0) {
        err(EXIT_FAILURE, "mount");
    }

    return EXIT_SUCCESS;
}
