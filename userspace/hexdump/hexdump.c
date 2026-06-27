#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BYTES_PER_LINE 16

static int do_canonical_hexdump(char* filename, int fd) {
    unsigned char buf[BYTES_PER_LINE];
    off_t offset = 0;

    ssize_t nread;
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        printf("%08jx  ", offset);

        for (ssize_t i = 0; i < BYTES_PER_LINE; i++) {
            if (i < nread) {
                printf("%02x ", buf[i]);
            } else {
                printf("   ");
            }

            if (i == 7) {
                putchar(' ');
            }
        }

        printf(" |");
        for (ssize_t i = 0; i < nread; i++) {
            putchar(isprint(buf[i]) ? buf[i] : '.');
        }

        for (ssize_t i = nread; i < BYTES_PER_LINE; i++) {
            putchar(' ');
        }
        printf("|\n");

        offset += nread;
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    if (offset > 0 && (offset % sizeof(buf)) != 0) {
        printf("%08jx\n", offset);
    }

    return EXIT_SUCCESS;
}

static int do_hexdump(const char* filename, int fd) {
    unsigned char buf[BYTES_PER_LINE];
    off_t offset = 0;

    ssize_t nread;
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        printf("%08jx  ", offset);

        for (ssize_t i = 0; i < nread; i += 2) {
            printf("%02x%02x", (i + 1 < nread) ? buf[i + 1] : 0, buf[i]);
            if (i + 2 < nread) {
                putchar(' ');
            }
        }

        putchar('\n');
        offset += nread;
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    if (offset > 0 && (offset % sizeof(buf)) != 0) {
        printf("%08jx\n", offset);
    }

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: hexdump [-C] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool canonical = false;

    int c;
    while ((c = getopt(argc, argv, "C")) != -1) {
        switch (c) {
            case 'C':
                canonical = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

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
            warn("%s", filename);
            ret = EXIT_FAILURE;
        } else {
            if (i != 0) {
                putchar('\n');
            }

            if (canonical) {
                ret |= do_canonical_hexdump(filename, fd);
            } else {
                ret |= do_hexdump(filename, fd);
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

    return ret;
}
