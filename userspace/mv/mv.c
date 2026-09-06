#define _GNU_SOURCE

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "file.h"
#include "util.h"

static bool force = false;
static bool interactive = false;
static bool verbose = false;

static inline bool prompt_overwrite(const char* path) {
    fprintf(stderr, "overwrite '%s'? ", path);
    return get_prompt();
}

static int do_move(const char* src_filename, int dest_dirfd, const char* dest_filename, const char* dest_path) {
    struct stat src_st;
    if (stat(src_filename, &src_st) < 0) {
        warn("failed to stat '%s'", src_filename);
        return EXIT_FAILURE;
    }

    bool dest_exists = true;

    struct stat dest_st;
    if (fstatat(dest_dirfd, dest_filename, &dest_st, 0) < 0) {
        if (errno != ENOENT) {
            warn("failed to stat '%s'", dest_path);
            return EXIT_FAILURE;
        }

        dest_exists = false;
    }

    if (dest_exists && interactive) {
        if (!prompt_overwrite(dest_path)) {
            return EXIT_SUCCESS;
        }
    }

    if (dest_exists && is_same_file(&src_st, &dest_st)) {
        warnx("'%s' and '%s' are the same file", src_filename, dest_path);
        return EXIT_FAILURE;
    }

    if (renameat(AT_FDCWD, src_filename, dest_dirfd, dest_filename) == 0) {
        if (verbose) {
            printf("renamed '%s' -> '%s'\n", src_filename, dest_path);
        }

        return EXIT_SUCCESS;
    } else if (errno != EXDEV) {
        warn("cannot rename '%s' to '%s'", src_filename, dest_path);
        return EXIT_FAILURE;
    }

    if (dest_exists && S_ISDIR(dest_st.st_mode) != S_ISDIR(src_st.st_mode)) {
        warnx("cannot overwrite '%s' with '%s'", dest_path, src_filename);
        return EXIT_FAILURE;
    }

    if (dest_exists) {
        if (unlinkat(dest_dirfd, dest_filename, S_ISDIR(dest_st.st_mode) ? AT_REMOVEDIR : 0) < 0) {
            warn("cannot unlink '%s'", dest_path);
            return EXIT_FAILURE;
        }
    }

    // TODO: do file copy and remove old src recursively

    warnx("MANUAL COPY NOT YET SUPPORTED :)");
    return EXIT_FAILURE;
}

static void usage(void) {
    fprintf(stderr, "usage: mv [-fiv] SRC DEST\n"
                    "       mv [-fiv] SRC... DIRECTORY\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "fiv")) != -1) {
        switch (c) {
            case 'f':
                force = true;
                interactive = false;
                break;
            case 'i':
                force = false;
                interactive = true;
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
        errx(EXIT_FAILURE, "missing source file operand");
    } else if (argc < 2) {
        errx(EXIT_FAILURE, "missing destination file operand");
    }

    const char* destination_path = argv[argc - 1];

    if (argc == 2) {
        struct stat dest_st;

        int res = stat(destination_path, &dest_st);
        if (res < 0 && errno != ENOENT) {
            err(EXIT_FAILURE, "failed to stat '%s'", destination_path);
        }

        if (res < 0 || !S_ISDIR(dest_st.st_mode)) {
            return do_move(argv[0], AT_FDCWD, destination_path, destination_path);
        }
    }

    int dest_dirfd = open(destination_path, O_PATH | O_DIRECTORY);
    if (dest_dirfd < 0) {
        err(EXIT_FAILURE, "cannot access '%s'", destination_path);
    }

    int ret = EXIT_SUCCESS;;

    for (int i = 0; i < argc - 1; i++) {
        const char* src_path = argv[i];

        char* src_copy = strdup(src_path);
        if (!src_copy) {
            err(EXIT_FAILURE, "strdup");
        }

        char* dest_name = basename(src_copy);
        if (strcmp(dest_name, "/") == 0) {
            dest_name = ".";
        }

        char* dest_path;
        if (asprintf(&dest_path, "%s/%s", destination_path, dest_name) < 0) {
            err(EXIT_FAILURE, "asprintf");
        }

        ret |= do_move(argv[i], dest_dirfd, dest_name, dest_path);

        free(dest_path);
        free(src_copy);
    }

    return ret;
}
