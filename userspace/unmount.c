#include <piggy/mount.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 

static void usage(void) {
    fprintf(stderr, "usage: unmount DIRECTORY\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing directory argument");
        usage();
    } else if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    if (umount(argv[0]) < 0) {
        err(EXIT_FAILURE, "failed to unmount '%s'", argv[0]);
    }

    return EXIT_SUCCESS;
}
