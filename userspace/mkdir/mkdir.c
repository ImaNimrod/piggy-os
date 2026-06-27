#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int mkdir_parents(const char* path) {
    size_t len = strlen(path);

    char* tmp = malloc(len + 1);
    if (!tmp) {
        err(EXIT_FAILURE, "malloc");
    }

    strcpy(tmp, path);

    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    char* p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';

            if (mkdir(tmp, 0777) < 0) {
                if (errno != EEXIST) {
                    warn("cannot create directory '%s'", tmp);
                    free(tmp);
                    return EXIT_FAILURE;
                }
            } else {
                printf("created directory '%s'\n", tmp);
            }

            *p = '/';
        }
    }

    if (mkdir(tmp, 0777) < 0) {
        if (errno != EEXIST) {
            free(tmp);
            return EXIT_FAILURE;
        }
    } else {
        printf("created directory '%s'\n", tmp);
    }

    free(tmp);
    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: mkdir [-p] DIRECTORY...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool create_parents = false;

    int c;
    while ((c = getopt(argc, argv, "p")) != -1) {
        switch (c) {
            case 'p':
                create_parents = true;
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

    int ret = EXIT_FAILURE;

    for (int i = 0; i < argc; i++) {
        if (create_parents) {
            ret = mkdir_parents(argv[i]);
        } else {
            if (mkdir(argv[i], 0777) < 0) {
                warn("cannot create directory '%s'", argv[i]);
                ret = EXIT_FAILURE;
            } else {
                printf("created directory '%s'\n", argv[i]);
            }
        }
    }

    return ret;
}
