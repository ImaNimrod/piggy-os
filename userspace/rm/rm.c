#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static bool force = false;
static bool interactive = false;
static bool verbose = false;

static bool prompt_remove(const char* path, struct stat* st) {
    char* type;
    switch (st->st_mode & S_IFMT) {
        case S_IFBLK:
            type = "block special file";
            break;
        case S_IFCHR:
            type = "character special file";
            break;
        case S_IFDIR:
            type = "directory";
            break;
        case S_IFREG:
            if (st->st_size == 0) {
                type = "regular empty file";
            } else {
                type = "regular file";
            }
            break;
        default:
            type = "unknown file";
            break;
    }

    fprintf(stderr, "remove %s '%s'? ", type, path);
    return get_prompt();
}

static int remove_recursive(const char* path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        if (!(force && errno == ENOENT)) {
            warn("failed to stat '%s'", path);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    if (interactive && !prompt_remove(path, &st)) {
        return EXIT_SUCCESS;
    }

    int ret = EXIT_SUCCESS;

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);

        struct dirent* dirent;
        while ((dirent = readdir(dir))) {
            if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0) {
                continue;
            }

            size_t path_len = strlen(path);
            size_t dir_len = strlen(dirent->d_name);

            char* dir_path = malloc(path_len + dir_len + 2);

            memcpy(dir_path, path, path_len);
            dir_path[path_len] = '/';
            memcpy(dir_path + path_len + 1, dirent->d_name, dir_len);
            dir_path[path_len + dir_len + 1] = '\0';

            if (dirent->d_type == DT_DIR) {
                ret = remove_recursive(dir_path);
            } else {
                if (interactive && !prompt_remove(dir_path, &st)) {
                    continue;
                }

                if (unlink(dir_path) < 0) {
                    warn("failed to remove '%s'", dir_path);
                    ret = EXIT_FAILURE;
                } else {
                    if (verbose) {
                        printf("removed '%s'\n", dir_path);
                    }
                }
            }

            free(dir_path);
        }

        closedir(dir);
    }

    if (unlink(path) < 0) {
        warn("failed to remove '%s'", path);
        return EXIT_FAILURE;
    }

    if (verbose) {
        printf("removed '%s'\n", path);
    }

    return ret;
}

static void usage(void) {
    fprintf(stderr, "usage: rm [-firv] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool recursive = false;

    int c;
    while ((c = getopt(argc, argv, "firv")) != -1) {
        switch (c) {
            case 'f':
                force = true;
                interactive = false;
                break;
            case 'i':
                force = false;
                interactive = true;
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

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        if (recursive) {
            ret = remove_recursive(argv[i]);
        } else {
            struct stat st;
            if (stat(argv[i], &st) < 0) {
                if (!(force && errno == ENOENT)) {
                    warn("%s", argv[i]);
                    ret = EXIT_FAILURE;
                }

                continue;
            }

            if (interactive && !prompt_remove(argv[i], &st)) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                errno = EISDIR;
                warn("cannot remove '%s'", argv[i]);
                ret = EXIT_FAILURE;
                continue;
            }

            if (unlink(argv[i]) < 0) {
                warn("failed to remove '%s'", argv[i]);
                ret = EXIT_FAILURE;
            } else if (verbose) {
                printf("removed '%s'\n", argv[i]);
            }
        }
    }

    return ret;
}
