#include <err.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: dirname [-z] NAME...\n");
}

int main(int argc, char* argv[]) {
    char delimiter = '\n';

    int c;
    while ((c = getopt(argc, argv, "z")) != -1) {
        switch (c) {
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

    char* dir;

    for (int i = 0; i < argc; i++) {
        if (!(dir = dirname(argv[i]))) {
            err(EXIT_FAILURE, "dirname");
        }

        printf("%s%c", dir, delimiter);
    }

    return EXIT_SUCCESS;
}
