#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRAM_NAME "truncate"

static bool no_create = false;
static bool block_mode = false;

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... SIZE FILE...\n\nShrink or extend the size of each FILE to the specified size. FILE arguments that do not exist are created.\nIf a FILE is larger than the given size, the extra data is lost.\nIf a FILE is shorter, it is extended and the newly extended portion is filled with zero bytes.\n\n-c\tdo not create any files\n-o\ttreat SIZE as a number of IO blocks rather than bytes\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static int do_truncate(char* path, off_t size, char set_mode) {
    int fd = open(path, O_WRONLY | (no_create ? 0 : O_CREAT));
    if (fd < 0) {
        if (!(no_create && errno == ENOENT)) {
            fputs(PROGRAM_NAME ": ", stderr);
            perror(path);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    int ret = EXIT_SUCCESS;

    struct stat st;

    if (fstat(fd, &st) < 0) {
        ret = EXIT_FAILURE;
        goto error;
    }

    ssize_t actual_size = 0;

    if (block_mode) {
        if (__builtin_mul_overflow(st.st_blksize, size, &size)) {
            fprintf(stderr, PROGRAM_NAME ": overflow when calculating new size of %s\n", path);
            ret = EXIT_FAILURE;
            goto end;
        }
    }

    switch (set_mode) {
        case '+':
            if (__builtin_add_overflow(st.st_size, size, &actual_size)) {
                fprintf(stderr, PROGRAM_NAME ": overflow when extending size of %s\n", path);
                ret = EXIT_FAILURE;
                goto end;
            }
            break;
        case '-':
            if (__builtin_sub_overflow(st.st_size, size, &actual_size)) {
                fprintf(stderr, PROGRAM_NAME ": overflow when reducing size of %s\n", path);
                ret = EXIT_FAILURE;
                goto end;
            }
            break;
        case '<':
            actual_size = actual_size > size ? actual_size : size;
            break;
        case '>':
            actual_size = actual_size < size ? actual_size : size;
            break;
        case '\0':
            actual_size = size;
            break;
    }

    if (ftruncate(fd, actual_size) < 0) {
        ret = EXIT_FAILURE;
        goto error;
    }

    goto end;

error:
    fputs(PROGRAM_NAME ": ", stderr);
    perror(path);
end:
    close(fd);
    return ret;
}

int main(int argc, char** argv) {
    int c;
    while (!(argv[optind] && argv[optind][0] == '-' && isdigit(argv[optind][1])) && (c = getopt(argc, argv, "cho")) != -1) {
        switch (c) {
            case 'c':
                no_create = true;
                break;
            case 'o':
                block_mode = true;
                break;
            case 'h': 
                help();
                break;
            case ':':
            case '?':
                error();
                break;
        }
    }

    if (optind >= argc) {
        fputs(PROGRAM_NAME ": missing size operand\n", stderr);
        error();
    }

    if (optind == argc - 1) {
        fputs(PROGRAM_NAME ": missing file operand\n", stderr);
        error();
    }

    char set_mode = '\0';

    char* size_arg = argv[optind];
    if (*size_arg == '+' || *size_arg == '-' || *size_arg == '<' || *size_arg == '>') {
        set_mode = *size_arg;
        size_arg++;
    }

    errno = 0;

    char* end_ptr;

    off_t size = strtol(size_arg, &end_ptr, 10);
    if (errno != 0 || size < 0 || *end_ptr) {
        fprintf(stderr, PROGRAM_NAME ": invalid size '%s'\n", size_arg);
        error();
    }

    int ret = EXIT_SUCCESS;

    for (int i = optind + 1; i < argc; i++) {
        ret = do_truncate(argv[i], size, set_mode);
    }

    return ret;
}
