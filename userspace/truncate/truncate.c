#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool io_blocks = false;
static bool no_create = false;

static int do_truncate(const char* path, off_t size, char size_operator) {
    int fd = open(path, O_WRONLY | (no_create ? 0 : O_CREAT));
    if (fd < 0) {
        if (!(no_create && errno == ENOENT)) {
            warn("cannot access '%s'", path);
        }

        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;

    struct stat st;

    if (fstat(fd, &st) < 0) {
        warn("%s", path);
        ret = EXIT_FAILURE;
        goto end;
    }

    off_t actual_size = 0;

    if (io_blocks) {
        if (__builtin_mul_overflow(st.st_blksize, size, &size)) {
            warnx("overflow when calculating new size of '%s'", path);
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    switch (size_operator) {
        case '+':
            if (__builtin_add_overflow(st.st_size, size, &actual_size)) {
                warnx("overflow when extending size of '%s'", path);
                ret = EXIT_FAILURE;
                goto end;
            }
            break;
        case '-':
            if (__builtin_sub_overflow(st.st_size, size, &actual_size)) {
                warnx("overflow when reducing size of '%s'", path);
                ret = EXIT_FAILURE;
                goto end;
            }
            break;
        case '<':
            actual_size = st.st_size > size ? size : st.st_size;
            break;
        case '>':
            actual_size = st.st_size < size ? size : st.st_size;
            break;
        case '\0':
            actual_size = size;
            break;
    }

    if (ftruncate(fd, actual_size) < 0) {
        warn("ftruncate(%s)", path);
        ret = EXIT_FAILURE;
        goto end;
    }

end:
    close(fd);
    return ret;
}

static void usage(void) {
    fprintf(stderr, "usage: truncate [-co] -s SIZE FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char size_operator = '\0';
    off_t size = -1;

    char* end_ptr;
    char* size_arg;

    int c;
    while ((c = getopt(argc, argv, "cos:")) != -1) {
        switch (c) {
            case 'c':
                no_create = true;
                break;
            case 'o':
                io_blocks = true;
                break;
            case 's':
                size_arg = optarg;
                if (*size_arg == '+' || *size_arg == '-' || *size_arg == '<' || *size_arg == '>') {
                    size_operator = *size_arg;
                    size_arg++;
                }

                errno = 0;

                size = strtol(size_arg, &end_ptr, 10);
                if (errno != 0 || size < 0 || *end_ptr) {
                    warnx("invalid size argument: '%s'", optarg);
                    usage();
                }
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

    if (size == -1) {
        warnx("missing size argument");
        usage();
    }

    int ret = EXIT_SUCCESS;

    for (int i = 0; i < argc; i++) {
        ret |= do_truncate(argv[i], size, size_operator);
    }

    return ret;
}
