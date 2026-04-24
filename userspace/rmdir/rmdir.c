#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int rmdir_parents(const char* path, bool verbose) {
    char* p = (char*) path + strlen(path);

    while (p > path && p[-1] == '/') {
        p--;
    }
    *p = '\0';

    *++p = '\0';
    while ((p = strrchr(path, '/')) != NULL) {
        while (p > path && p[-1] == '/') {
            p--;
        }
        *p = '\0';

        if (path[0] == '\0') {
            break;
        }

        if (rmdir(path) < 0) {
            warn(path);
            return EXIT_FAILURE;
        }

        if (verbose) {
            puts(path);
        }
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: rmdir [-pv] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool remove_parents = false;
    bool verbose = false;

    int c;
    while ((c = getopt(argc, argv, "pv")) != -1) {
        switch (c) {
            case 'p':
                remove_parents = true;
                break;
            case 'v':
                verbose = true;
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

    for (int i = 0; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            warn(argv[i]);
            ret = EXIT_FAILURE;
        } else {
            if (verbose) {
                puts(argv[i]);
            }

            if (remove_parents) {
                ret |= rmdir_parents(argv[i], verbose);
            }
        }
    }

    return ret;
}
