#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "wc"

#define WC_BUF_SIZE 1024

enum {
    PRINT_CHARS = (1 << 0),
    PRINT_LINES = (1 << 1),
    PRINT_WORDS = (1 << 2),
    PRINT_ALL   = PRINT_CHARS | PRINT_LINES | PRINT_WORDS
};

static char buf[WC_BUF_SIZE];
static int print_mode = PRINT_ALL;
static size_t total_chars = 0, total_lines = 0, total_words = 0;

static void print_error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
}

static void print_help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [FILE]...\n\nDisplay the newline, word, and byte counts for each FILE.\n\n-c\tprint the byte counts\n-m\tprint the character counts\n-l\tprint the newline counts\n-h\tdisplay this help and exit\n");
}

static int wc(char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fputs(PROGRAM_NAME ": ", stderr);
        perror(path);
        return EXIT_FAILURE;
    }

    size_t c = 0, l = 0, w = 0;
    bool in_word = false;

    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                l++;
            }

            if (strchr(" \r\n\t\v", buf[i])) {
                in_word = false;
            } else if (!in_word) {
                w++;
                in_word = true;
            }

            c++;
        }
    }

    if (n < 0) {
        fputs(PROGRAM_NAME ": ", stderr);
        perror(path);
        return EXIT_FAILURE;
    }

    close(fd);

    putchar(' ');
    if (print_mode & PRINT_CHARS) {
        printf("%zu  ", c);
    }
    if (print_mode & PRINT_LINES) {
        printf("%zu  ", l);
    }
    if (print_mode & PRINT_WORDS) {
        printf("%zu  ", w);
    }
    puts(path);
    putchar('\n');

    total_chars += c;
    total_lines += l;
    total_words += w;

    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    int c;
    while ((c = getopt(argc, argv, "chlw")) != -1) {
        switch (c) {
            case 'c':
                print_mode = PRINT_CHARS;
                break;
            case 'l':
                print_mode = PRINT_LINES;
                break;
            case 'w':
                print_mode = PRINT_WORDS;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_error();
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        print_error();
        return EXIT_FAILURE;
    }

    int ret = EXIT_SUCCESS;

    for (int i = optind; i < argc; i++) {
        ret = wc(argv[i]);
    }

    if (optind + 1 < argc) {
        putchar(' ');
        if (print_mode & PRINT_CHARS) {
            printf("%zu  ", total_chars);
        }
        if (print_mode & PRINT_LINES) {
            printf("%zu  ", total_lines);
        }
        if (print_mode & PRINT_WORDS) {
            printf("%zu  ", total_words);
        }
        puts("total\n");
    }

    return ret;
}
