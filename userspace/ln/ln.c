#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static bool force = false;
static bool interactive = false;
static bool symbolic = false;
static bool verbose = false;

static inline bool prompt_overwrite(const char* path) {
    fprintf(stderr, "replace '%s'? ", path);
    return get_prompt();
}

static bool do_link(const char* target, const char* linkname) {
    bool retried = false;

retry:
    if ((symbolic ? symlink(target, linkname) : link(target, linkname)) < 0) {
        if (!retried && errno == EEXIST) {
            bool replace = force;
            if (interactive) {
                replace = prompt_overwrite(linkname);
            }

            if (replace) {
                if (unlink(linkname) < 0) {
                    warn("%s", linkname);
                    return false;
                }

                retried = true;
                goto retry;
            }
        }

        warn("%s", linkname);
        return false;
    }

    if (verbose) {
        printf("%s -> %s\n", linkname, target);
    }

    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: ln [-fisv] TARGET LINKNAME\n"
                    "       ln [-fisv] TARGET... DIRECTORY\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "fisv")) != -1) {
        switch (c) {
            case 'f':
                force = true;
                interactive = false;
                break;
            case 'i':
                force = false;
                interactive = true;
                break;
            case 's':
                symbolic = true;
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

    if (argc < 2) {
        warnx("missing operand");
        usage();
    }

    if (argc == 2) {
        return do_link(argv[0], argv[1]) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    struct stat st;
    if (stat(argv[argc - 1], &st) < 0 || !S_ISDIR(st.st_mode)) {
        errx(EXIT_FAILURE, "'%s' is not a directory", argv[argc - 1]);
    }

    int ret = EXIT_SUCCESS;

    const char* target = argv[argc - 1];

    for (int i = 0; i < argc - 1; i++) {
        const char* source = argv[i];

        char* source_copy = strdup(source);
        if (source_copy == NULL) {
            err(EXIT_FAILURE, "strdup");
        }

        const char* base_name = basename(source_copy);
        size_t source_length = strlen(source);

        bool has_slash = source_length && source[source_length - 1] == '/';

        char* new_target;
        if (asprintf(&new_target, "%s%s%s", target, has_slash ? "" : "/", base_name) < 0) {
            err(EXIT_FAILURE, "asprintf");
        }

        if (!do_link(argv[i], new_target)) {
            ret = EXIT_FAILURE;
        }

        free(new_target);
        free(source_copy);
    }

    return ret;
}
