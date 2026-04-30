#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static bool force = false;
static bool interactive = false;
static bool preserve = false;
static bool recursive = false;
static bool verbose = false;

static inline bool prompt_overwrite(const char* path) {
    fprintf(stderr, "overwrite '%s'? ", path);
    return get_prompt();
}

static int do_copy(int src_dirfd, const char* src_filename, const char* src_path, int dest_dirfd, const char* dest_filename, const char* dest_path) {
    struct stat src_st;
    if (fstatat(src_dirfd, src_filename, &src_st, 0) < 0) {
        warn("failed to stat '%s'", src_path);
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

    if (dest_exists && is_same_file(&src_st, &dest_st)) {
        warnx("'%s' and '%s' are the same file", src_path, dest_path);
        return EXIT_FAILURE;
    }

    int dest_fd;

    if (S_ISDIR(src_st.st_mode)) {
        if (!recursive) {
            warnx("cannot copy '%s', -r not specified", src_path);
            return EXIT_FAILURE;
        }

        if (dest_exists && !S_ISDIR(dest_st.st_mode)) {
            warnx("cannot overwrite file '%s' with directory '%s'", dest_path, src_path);
            return EXIT_FAILURE;
        }

        if (!dest_exists) {
            if (mkdirat(dest_dirfd, dest_path, 0777) < 0) {
                warn("mkdirat(%s)", dest_path);
                return EXIT_FAILURE;
            }

            if (fstatat(dest_dirfd, dest_path, &dest_st, 0) < 0) {
                warn("fstatat(%s)", dest_path);
                return EXIT_FAILURE;
            }
        }

        int src_fd = openat(src_dirfd, src_filename, O_RDONLY | O_DIRECTORY);
        if (src_fd < 0) {
            warn("openat(%s)", src_path);
            return EXIT_FAILURE;
        }

        DIR* dir = fdopendir(src_fd);
        if (dir == NULL) {
            warn("fdopendir(%s)", src_path);
            close(src_fd);
            return EXIT_FAILURE;
        }

        dest_fd = openat(dest_dirfd, dest_filename, O_PATH | O_DIRECTORY);
        if (dest_fd < 0) {
            warn("openat(%s)", dest_path);
            return EXIT_FAILURE;
        }

        struct dirent* entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                entry = readdir(dir);
                continue;
            }

            char* new_src_path = NULL;
            char* new_dest_path = NULL;

            asprintf(&new_src_path, "%s/%s", src_path, entry->d_name);
            if (new_src_path == NULL) {
                err(EXIT_FAILURE, "asprintf");
            }

            asprintf(&new_dest_path, "%s/%s", dest_path, entry->d_name);
            if (new_dest_path == NULL) {
                err(EXIT_FAILURE, "asprintf");
            }

            do_copy(src_fd, entry->d_name, new_src_path, dest_fd, entry->d_name, new_dest_path);

            free(new_src_path);
            free(new_dest_path);
        }

        closedir(dir);
    } else if (S_ISREG(src_st.st_mode)) {
        if (dest_exists) {
            if (interactive && !prompt_overwrite(dest_path)) {
                return EXIT_SUCCESS;
            }

            dest_fd = openat(dest_dirfd, dest_filename, O_WRONLY | O_TRUNC);
            if (dest_fd < 0) {
                if (force) {
                    if (unlinkat(dest_dirfd, dest_filename, 0) < 0) {
                        warn("unlinkat(%s)", dest_path);
                        return EXIT_FAILURE;
                    }

                    dest_exists = false;
                } else {
                    warn("open(%s)", dest_path);
                    return EXIT_FAILURE;
                }
            }
        }

        if (!dest_exists) {
            dest_fd = openat(dest_dirfd, dest_filename, O_WRONLY | O_CREAT, 0);
            if (dest_fd < 0) {
                warn("open(%s)", dest_path);
                return EXIT_FAILURE;
            }
        }

        int src_fd = openat(src_dirfd, src_filename, O_RDONLY);
        if (src_fd < 0) {
            warn("open(%s)", src_path);
            close(dest_fd);
            return EXIT_FAILURE;
        }

        if (!file_copy(src_fd, src_path, dest_fd, dest_path)) {
            close(src_fd);
            close(dest_fd);
            return EXIT_FAILURE;
        } else if (verbose) {
            printf("'%s' -> '%s'\n", src_path, dest_path);
        }

        close(src_fd);
    } else {
        warnx("unsupported file type: '%s'", src_path);
        return EXIT_FAILURE;
    }

    if (preserve) {
        struct timespec ts[2] = { src_st.st_atim, src_st.st_mtim };
        if (futimens(dest_fd, ts) < 0) {
            warn("futimens(%s)", dest_path);
            return EXIT_FAILURE;
        }
    }

    close(dest_fd);
    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: cp [-fiprv] SOURCE DESTINATION\n"
                    "       cp [-fiprv] SOURCE... DIRECTORY\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "fiprv")) != -1) {
        switch (c) {
            case 'f':
                force = true;
                interactive = false;
                break;
            case 'i':
                force = false;
                interactive = true;
                break;
            case 'p':
                preserve = true;
                break;
            case 'r':
                recursive = true;
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
        struct stat st;
        if (stat(destination_path, &st) < 0 || !S_ISDIR(st.st_mode)) {
            return do_copy(AT_FDCWD, argv[0], argv[0], AT_FDCWD, destination_path, destination_path);
        }
    }

    int dest_fd = open(destination_path, O_PATH | O_DIRECTORY);
    if (dest_fd < 0) {
        err(EXIT_FAILURE, "open(%s)", destination_path);
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc - 1; i++) {
        const char* src_path = argv[i];

        char* src_copy = strdup(src_path);
        if (src_copy == NULL) {
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

        ret |= do_copy(AT_FDCWD, src_path, src_path, dest_fd, dest_name, dest_path);

        free(src_copy);
        free(dest_path);
    }

    return ret;
}
