#include <sys/stat.h> 

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: touch [-acm] FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool modify_atime = true;
    bool modify_mtime = true;
    bool no_create = false;

    int c;
    while ((c = getopt(argc, argv, "acm")) != -1) {
        switch (c) {
            case 'a':
                modify_atime = true;
                modify_mtime = false;
                break;
            case 'c':
                no_create = true;
                break;
            case 'm':
                modify_atime = false;
                modify_mtime = true;
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_NOW;
    ts[1].tv_nsec = UTIME_NOW;

    if (!modify_atime) {
        ts[0].tv_nsec = UTIME_OMIT;
    }
    if (!modify_mtime) {
        ts[1].tv_nsec = UTIME_OMIT;
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        if (utimensat(AT_FDCWD, argv[i], ts, 0) < 0) {
            if (!no_create) {
                if (errno == ENOENT) {
                    int fd = open(argv[i], O_WRONLY | O_CREAT);
                    if (fd < 0) {
                        warn("open: '%s'", argv[i]);
                        ret = EXIT_FAILURE;
                        continue;
                    }

                    if (futimens(fd, ts) < 0) {
                        warn("futimens: '%s'", argv[i]);
                        ret = EXIT_FAILURE;
                    }

                    close(fd);
                } else {
                    warn("utimensat: '%s'", argv[i]);
                    ret = EXIT_FAILURE;
                }
            }
        }
    }

    return ret;
}
