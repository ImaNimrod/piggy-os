#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE (4 * 1024)

static char* buf;

static int tail_bytes(const char* filename, int fd, ssize_t count) {
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == -1) {
        warn("lseek(%s)", filename);
        return EXIT_FAILURE;
    }

    off_t start = (file_size > count) ? file_size - count : 0;
    lseek(fd, start, SEEK_SET);

    ssize_t nread;
    while ((nread = read(fd, buf, BUFSIZE)) > 0) {
        if (write(STDOUT_FILENO, buf, nread) != nread) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int tail_lines(const char* filename, int fd, ssize_t count, char line_delimiter) {
    if (count == 0) {
        return EXIT_SUCCESS;
    }

    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos == -1) {
        warn("lseek(%s)", filename);
        return EXIT_FAILURE;
    }

    ssize_t line_count = 0;
    while (pos > 0 && line_count <= count) {
        off_t step = (pos >= BUFSIZE) ? BUFSIZE : pos;
        pos -= step;
        lseek(fd, pos, SEEK_SET);

        ssize_t nread = read(fd, buf, step);
        for (ssize_t i = nread - 1; i >= 0; i--) {
            if (buf[i] == line_delimiter) {
                line_count++;
                if (line_count > count) {
                    pos += i + 1;
                    goto done;
                }
            }
        }
    }

done:
    lseek(fd, pos, SEEK_SET);

    ssize_t nread;
    while ((nread = read(fd, buf, BUFSIZE)) > 0) {
        if (write(STDOUT_FILENO, buf, nread) != nread) {
            err(EXIT_FAILURE, "write(stdout)");
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: tail [-qvz] [-c BYTES | -n LINES] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    ssize_t byte_count = -1;
    ssize_t line_count = -1;
    char line_delimiter = '\n';
    bool quiet = false;
    bool verbose = false;

    char* end_ptr;
    uintmax_t value;

    int c;
    while ((c = getopt(argc, argv, "c:n:qvz")) != -1) {
        switch (c) {
            case 'c':
                errno = 0;

                value = strtoumax(optarg, &end_ptr, 10);
                if (errno != 0 || optarg == end_ptr || *end_ptr || value == 0 || value > SSIZE_MAX) {
                    warnx("invalid byte count: '%s'", optarg);
                    usage();
                }

                byte_count = (ssize_t) value;
                break;
            case 'n':
                errno = 0;

                value = strtoumax(optarg, &end_ptr, 10);
                if (errno != 0 || optarg == end_ptr || *end_ptr || value == 0 || value > SSIZE_MAX) {
                    warnx("invalid line count: '%s'", optarg);
                    usage();
                }

                line_count = (ssize_t) value;
                break;
            case 'q':
                quiet = true;
                verbose = false;
                break;
            case 'v':
                quiet = false;
                verbose = true;
                break;
            case 'z':
                line_delimiter = '\0';
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (byte_count != -1 && line_count != -1) {
        warnx("cannot combine byte and line counts");
        usage();
    }

    if (line_count == -1) {
        line_count = 10;
    }

    if (!(buf = malloc(BUFSIZE))) {
        errx(EXIT_FAILURE, "malloc");
    }

    int ret = EXIT_SUCCESS;

    char* filename;
    int fd;

    int i = 0;
    while (argv[i] || i == 0) {
        if (!argv[i] || strcmp(argv[i], "-") == 0) {
            filename = "stdin";
            fd = STDIN_FILENO;
        } else {
            filename = argv[i];
            fd = open(filename, O_RDONLY);
        }

        if (fd < 0) {
            warn("cannot open '%s'", filename);
            ret = EXIT_FAILURE;
        } else {
            if (verbose || (!quiet && argc > 1)) {
                if (i > 0) {
                    putchar('\n');
                }
                printf("==| %s |==\n", filename);
            }

            if (byte_count == -1) {
                ret |= tail_lines(filename, fd, line_count, line_delimiter);
            } else {
                ret |= tail_bytes(filename, fd, byte_count);
            }

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (!argv[i]) {
            break;
        }

        i++;
    }

    free(buf);
    return ret;
}
