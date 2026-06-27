#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: realpath [-qz] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char delimiter = '\n';
    bool quiet = false;

    int c;
    while ((c = getopt(argc, argv, "qz")) != -1) {
        switch (c) {
            case 'q':
                quiet = true;
                break;
            case 'z':
                delimiter = '\0';
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    int ret = EXIT_SUCCESS;
    char* path;

    for (int i = 0; i < argc; i++) {
        if (!(path = realpath(argv[i], NULL))) {
            if (!quiet) {
                warn("%s", argv[i]);
            }

            ret = EXIT_FAILURE;
            continue;
        }

        printf("%s%c", path, delimiter);
    }

    return ret;
}
