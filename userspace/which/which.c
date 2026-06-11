#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline bool check_file(const char* path) {
    int fd = open(path, O_RDONLY | O_PATH);
    if (fd >= 0) {
        close(fd);
        return true;
    }

    return false;
}

static void usage(void) {
    fprintf(stderr, "usage: which [-a] NAME...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool print_all = false;

    int c;
    while ((c = getopt(argc, argv, "a")) != -1) {
        switch (c) {
            case 'a':
                print_all = true;
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
        if (strchr(argv[i], '/') != NULL) {
            if (check_file(argv[i])) {
                puts(argv[i]);
            } else {
                printf("%s not found\n", argv[i]);
                ret = EXIT_FAILURE;
            }
        } else {
            char* path = getenv("PATH");
            if (path == NULL) {
                path = _PATH_DEFPATH;
            }

            path = strdup(path);
            if (path == NULL) {
                err(EXIT_FAILURE, "strdup");
            }

            bool found = false;

            char* p;
            char* last;
            for ((p = strtok_r(path, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
                char* file;
                if (asprintf(&file, "%s/%s", p, argv[i]) < 0) {
                    err(EXIT_FAILURE, "asprintf");
                }

                if (!check_file(file)) {
                    free(path);
                    continue;
                }

                found = true;
                puts(file);

                free(file);

                if (!print_all) {
                    break;
                }
            }

            free(path);

            if (!found) {
                printf("%s not found\n", argv[i]);
                ret = EXIT_FAILURE;
            }
        }
    }

    return ret;
}
