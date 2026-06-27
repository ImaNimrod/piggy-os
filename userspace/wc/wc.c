#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE (1024 * 8)

#define PRINT_CHARS (1 << 0)
#define PRINT_LINES (1 << 1)
#define PRINT_WORDS (1 << 2)
#define PRINT_ALL   PRINT_CHARS | PRINT_LINES | PRINT_WORDS

static char* buf;
static int print_mode;
static size_t total_chars, total_lines, total_words;

static int do_wc(const char* filename, int fd) {
    size_t c = 0, l = 0, w = 0;
    bool in_word = false;

    struct stat stat;
    if (fstat(fd, &stat) < 0) {
        warn("failed to stat '%s'", filename);
        return EXIT_FAILURE;
    }

    c = stat.st_size;

    ssize_t nread;
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < nread; i++) {
            if (buf[i] == '\n') {
                l++;
            }

            if (strchr(" \r\n\t\v", buf[i])) {
                in_word = false;
            } else if (!in_word) {
                w++;
                in_word = true;
            }
        }
    }

    if (nread < 0) {
        warn("%s", filename);
        return EXIT_FAILURE;
    }

    if (print_mode & PRINT_LINES) {
        printf("%zu  ", l);
    }
    if (print_mode & PRINT_WORDS) {
        printf("%zu  ", w);
    }
    if (print_mode & PRINT_CHARS) {
        printf("%zu  ", c);
    }
    puts(filename);

    total_chars += c;
    total_lines += l;
    total_words += w;

    return EXIT_SUCCESS;
}

static void usage(void) {
    fprintf(stderr, "usage: wc [-clw] [FILE]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    int c;
    while ((c = getopt(argc, argv, "clw")) != -1) {
        switch (c) {
            case 'c':
                print_mode |= PRINT_CHARS;
                break;
            case 'l':
                print_mode |= PRINT_LINES;
                break;
            case 'w':
                print_mode |= PRINT_WORDS;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (print_mode == 0) {
        print_mode = PRINT_ALL;
    }

    if (!(buf = malloc(BUFSIZE))) {
        err(EXIT_FAILURE, "malloc");
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
            ret |= do_wc(filename, fd);

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (!argv[i]) {
            break;
        }

        i++;
    }

    if (i > 1) {
        if (print_mode & PRINT_LINES) {
            printf("%zu  ", total_lines);
        }
        if (print_mode & PRINT_WORDS) {
            printf("%zu  ", total_words);
        }
        if (print_mode & PRINT_CHARS) {
            printf("%zu  ", total_chars);
        }
        puts("total");
    }

    free(buf);
    return ret;
}
