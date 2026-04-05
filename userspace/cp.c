#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int do_file_copy(int src_fd, const char* src_path, int dest_fd, const char* dest_path);
static bool prompt_copy(const char* path); 

static int do_copy(int src_dirfd, const char* src_path, int dest_dirfd, const char* dest_path, bool force, bool interactive, bool preserve) {
    struct stat src_st, dest_st;

    if (fstatat(src_dirfd, src_path, &src_st, 0) < 0) {
        warn("failed to stat '%s'", src_path);
        return EXIT_FAILURE;
    }

    bool dest_exists = true;

    if (fstatat(dest_dirfd, dest_path, &dest_st, 0) < 0) {
        if (errno != ENOENT) {
            warn("failed to stat '%s'", dest_path);
            return EXIT_FAILURE;
        }

        dest_exists = false;
    }

    if (dest_exists && src_st.st_dev == dest_st.st_dev && src_st.st_ino == dest_st.st_ino) {
        warnx("'%s' and '%s' are the same file", src_path, dest_path);
        return EXIT_FAILURE;
    }

    int dest_fd;

    if (S_ISDIR(src_st.st_mode)) {
        warnx("unsupported file type: '%s'", src_path);
        return EXIT_FAILURE;
    } else if (S_ISREG(src_st.st_mode)) {
        if (dest_exists) {
            if (interactive && !prompt_copy(dest_path)) {
                return EXIT_SUCCESS;
            }

            dest_fd = openat(dest_dirfd, dest_path, O_WRONLY | O_TRUNC);
            if (dest_fd < 0) {
                if (force) {
                    if (unlinkat(dest_dirfd, dest_path, 0) < 0) {
                        warn("unlinkat: '%s'", dest_path);
                        return EXIT_FAILURE;
                    }

                    dest_exists = false;
                } else {
                    warn("open: '%s'", dest_path);
                    return EXIT_FAILURE;
                }
            }
        }

        if (!dest_exists) {
            dest_fd = openat(dest_dirfd, dest_path, O_WRONLY | O_CREAT, 0);
            printf("openat(%d, %s) -> %d\n", dest_dirfd, dest_path, dest_fd);
            if (dest_fd < 0) {
                warn("open: '%s'", dest_path);
                return EXIT_FAILURE;
            }
        }

        int src_fd = openat(src_dirfd, src_path, O_RDONLY);
        if (src_fd < 0) {
            warn("open: '%s'", src_path);
            close(dest_fd);
            return EXIT_FAILURE;
        }

        if (do_file_copy(src_fd, src_path, dest_fd, dest_path) < 0) {
            close(src_fd);
            close(dest_fd);
            return EXIT_FAILURE;
        }

        close(src_fd);
    } else {
        warnx("unsupported file type: '%s'", src_path);
        return EXIT_FAILURE;
    }

    if (preserve) {
        struct timespec ts[2] = { src_st.st_atim, src_st.st_mtim };
        if (futimens(dest_fd, ts) < 0) {
            warn("futimens: '%s'", dest_path);
        }
    }

    close(dest_fd);
    return EXIT_SUCCESS;
}

static int do_file_copy(int src_fd, const char* src_path, int dest_fd, const char* dest_path) {
    for (;;) {
        char buffer[4096];

        ssize_t nread = read(src_fd, buffer, sizeof(buffer));
        if (nread < 0) {
            warn("read: '%s'", src_path);
            return EXIT_FAILURE;
        } else if (nread == 0) {
            return EXIT_SUCCESS;
        }

        while (nread > 0) {
            ssize_t nwritten = write(dest_fd, buffer, nread);
            if (nwritten < 0) {
                warn("write: '%s'", dest_path);
                return EXIT_FAILURE;
            }

            nread -= nwritten;
        }
    }
}

static bool prompt_copy(const char* path) {
    fprintf(stderr, "overwrite '%s'? ", path);

    char buf[64];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return false;
    }

    return buf[0] == 'Y' || buf[0] == 'y';
}

static void usage(void) {
    fprintf(stderr, "usage: cp [-fip] SOURCE... DESTINATION\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool force = false;
    bool interactive = false;
    bool preserve = false;

    int c;
    while ((c = getopt(argc, argv, "fip")) != -1) {
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
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        errx(EXIT_FAILURE, "missing source file operand");
    } else if (argc < 2) {
        errx(EXIT_FAILURE, "missing destination file operand after '%s'", argv[0]);
    }

    const char* destination_path = argv[argc - 1];

    if (argc == 2) {
        struct stat st;
        if (stat(destination_path, &st) < 0 || !S_ISDIR(st.st_mode)) {
            return do_copy(AT_FDCWD, argv[0], AT_FDCWD, destination_path, force, interactive, preserve);
        }
    }

    int ret = EXIT_SUCCESS;

    int dest_fd = open(destination_path, O_PATH | O_DIRECTORY);
    if (dest_fd < 0) {
        err(EXIT_FAILURE, "open: '%s'", destination_path);
    }

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

        char* dest_path = malloc(strlen(destination_path) + strlen(dest_name) + 2);
        if (dest_path == NULL) {
            err(EXIT_FAILURE, "malloc");
        }

        stpcpy(stpcpy(stpcpy(dest_path, destination_path), "/"), dest_name);

        ret |= do_copy(AT_FDCWD, src_path, dest_fd, dest_path, force, interactive, preserve);

        free(src_copy);
        free(dest_path);
    }

    return ret;
}
