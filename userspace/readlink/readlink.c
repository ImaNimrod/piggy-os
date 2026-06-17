#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: readlink [-fz] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool canonicalize = false;
    char delimiter = '\n';
    bool quiet = false;

    int c;
    while ((c = getopt(argc, argv, "fqz")) != -1) {
        switch (c) {
            case 'f':
                canonicalize = true;
                break;
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

    if (canonicalize) {
        for (int i = 0; i < argc; i++) {
            char* path;

            if ((path = realpath(argv[i], NULL)) == NULL) {
                if (!quiet) {
                    warn("%s", argv[i]);
                }
                ret = EXIT_FAILURE;
                continue;
            }

            printf("%s%c", path, delimiter);
        }
    } else {
        char buffer[PATH_MAX];

        for (int i = 0; i < argc; i++) {
            ssize_t nread = readlink(argv[i], buffer, PATH_MAX - 1);
            if (nread < 0) {
                if (!quiet) {
                    warn("%s", argv[i]);
                }
                ret = EXIT_FAILURE;
                continue;
            }

            buffer[nread] = '\0';

            printf("%s%c", buffer, delimiter);
        }
    }


    return ret;
}
