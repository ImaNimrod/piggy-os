#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE (1024 * 8)

enum {
    PRINT_CHARS = 0x01,
    PRINT_LINES = 0x02, 
    PRINT_WORDS = 0x04,
    PRINT_ALL   = PRINT_CHARS | PRINT_LINES | PRINT_WORDS,
};

static char* buf;
static int print_mode;
static size_t total_chars, total_lines, total_words;

static int do_wc(char* filename, int fd) {
    size_t c = 0, l = 0, w = 0;
    bool in_word = false;

    struct stat stat;
    if (fstat(fd, &stat) < 0) {
        warn("failed to stat %s", filename);
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
        warn(filename);
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
    fprintf(stderr, "usage: wc [-clw] [FILE]...\n"
            "With no FILE, or when FILE is -, read from stdin.\n");
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
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (print_mode == 0) {
        print_mode = PRINT_ALL;
    }

    if ((buf = malloc(BUFSIZE)) == NULL) {
        err(EXIT_FAILURE, "malloc");
    }

    int ret = EXIT_SUCCESS;

    int fd;
    char* filename;
    char* path;

    int i = 0;
    while ((path = argv[i]) != NULL || i == 0) {
        if (path == NULL || strcmp(path, "-") == 0) {
            fd = STDIN_FILENO;
            filename = "stdin";
        } else {
            fd = open(path, O_RDONLY);
            filename = path;
        }

        if (fd < 0) {
            warn(filename);
            ret = EXIT_FAILURE;
        } else {
            ret = do_wc(filename, fd);

            if (fd != STDIN_FILENO) {
                close(fd);
            }
        }

        if (path == NULL) {
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
