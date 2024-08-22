#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRAM_NAME "head"

static bool print_chars = false;
static ssize_t byte_count = 0;
static ssize_t line_count = 10;

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [FILE]...\n\nPrint the first 10 lines of each FILE to stdout. With more than one FILE, precede each with a header displaying the filename.\nWith no FILE, or when FILE is -, read stdin.\n\n-c NUM\tprint the first NUM bytes of each file.\n-n\tprint the first NUM lines of each file instead of the first 10\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

static int head(char* path) {
    int ret = EXIT_SUCCESS;

    int fd;
    if (!strcmp(path, "-")) {
        fd = STDIN_FILENO;
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            ret = EXIT_FAILURE;
            return ret;
        }
    }

    char* buf = NULL;

    if (print_chars) {
        struct stat st;
        if (fstat(fd, &st) < 0) {
            ret = EXIT_FAILURE;
            goto error;
        }

        ssize_t actual_count = st.st_size < byte_count ? st.st_size : byte_count;

        buf = malloc(actual_count);
        if (buf == NULL) {
            ret = EXIT_FAILURE;
            goto error;
        }

        if (read(fd, buf, actual_count) < 0) {
            ret = EXIT_FAILURE;
            goto error;
        }

        if (write(STDOUT_FILENO, buf, actual_count) != actual_count) {
            ret = EXIT_FAILURE;
            goto error;
        }
    } else {
        ssize_t count = 0, n;

        buf = malloc(1024);
        if (buf == NULL) {
            ret = EXIT_FAILURE;
            goto error;
        }

        for (;;) {
            if ((n = read(fd, buf, 1024)) < 0) {
                ret = EXIT_FAILURE;
                goto error;
            }

            for (ssize_t i = 0; i < n; i++) {
                if (count == line_count) {
                    break;
                }

                if (buf[i] == '\n') {
                    count++;
                }
                putchar(buf[i]);
            }

            if (count == line_count || n < 1024) {
                break;
            }
        }
    }

    goto end;

error:
    fputs(PROGRAM_NAME ": ", stderr);
    perror(path);
end:
    if (buf) {
        free(buf);
    }
    close(fd);
    return ret;
}

int main(int argc, char** argv) {
    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "c:hn:")) != -1) {
        switch (c) {
            case 'c':
                print_chars = true;
                errno = 0;
                byte_count = strtol(optarg, &end_ptr, 10);
                if (errno != 0 || byte_count < 0 || optarg == end_ptr) {
                    fprintf(stderr, PROGRAM_NAME ": invalid number of bytes: '%s'\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'n':
                print_chars = false;
                errno = 0;
                line_count = strtol(optarg, &end_ptr, 10);
                if (errno != 0 || line_count < 0 || optarg == end_ptr) {
                    fprintf(stderr, PROGRAM_NAME ": invalid number of lines: '%s'\n", optarg);
                    return EXIT_FAILURE;
                }
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

    int ret = EXIT_SUCCESS;

    if (optind == argc - 1) {
        ret = head(argv[argc - 1]);
    } else if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            printf("==| %s |==\n", argv[i]);

            ret = head(argv[i]);

            if (i != argc - 1) {
                putchar('\n');
            }
        }
    } else {
        ret = head("-");
    }

    return ret;
}
