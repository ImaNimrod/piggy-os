#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct page_buffer {
    char** lines;
    size_t capacity;
    size_t size;

    FILE* fp;
    bool eof;
};

static int tty_fd = -1;
static struct termios old_termios;
static size_t height, width;

static void buffer_add(struct page_buffer* buf, const char* line) {
    if (buf->size == buf->capacity) {
        buf->capacity = buf->capacity != 0 ? buf->capacity * 2 : 128;

        buf->lines = realloc(buf->lines, buf->capacity * sizeof(char*));
        if (buf->lines == NULL) {
            err(EXIT_FAILURE, "realloc");
        }
    }

    buf->lines[buf->size++] = strdup(line);
}

static int buffer_ensure(struct page_buffer* buf, size_t index) {
    if (index < buf->size) {
        return 1;
    }

    if (buf->eof) {
        return 0;
    }

    char* line = NULL;
    size_t n = 0;

    while (buf->size <= index) {
        ssize_t r = getline(&line, &n, buf->fp);
        if (r == -1) {
            buf->eof = true;
            free(line);
            return index < buf->size;
        }

        buffer_add(buf, line);
    }

    free(line);
    return 1;
}

static void buffer_load_all(struct page_buffer* buf) {
    char* line = NULL;
    size_t n = 0;

    while (!buf->eof) {
        if (getline(&line, &n, buf->fp) == -1) {
            buf->eof = true;
            break;
        }

        buffer_add(buf, line);
    }

    free(line);
}

static void buffer_free(struct page_buffer* buf) {
    for (size_t i = 0; i < buf->size; i++) {
        free(buf->lines[i]);
    }
    free(buf->lines);

    if (buf->fp != stdin) {
        fclose(buf->fp);
    }
}

static void page_content(struct page_buffer* buf) {
    size_t top = 0;

    while (1) {
        printf("\033[H\033[J");

        for (size_t i = 0; i < height - 1; i++) {
            size_t idx = top + i;

            if (!buffer_ensure(buf, idx)) {
                break;
            }

            fputs(buf->lines[idx], stdout);
        }

        if (buf->eof && top + (height - 1) >= buf->size) {
            break;
        }

        printf("--more--");
        fflush(stdout);

        char c;
        if (read(tty_fd, &c, 1) <= 0) {
            break;
        }

        printf("\r\033[K");
        fflush(stdout);

        switch (c) {
            case ' ': // down a page
                top += height - 1;
                break;
            case 'b': // up a page
                if (top >= height - 1) {
                    top -= (height - 1);
                } else {
                    top = 0;
                }
                break;
            case 'd': // down half a page
                top += (height - 1) / 2;
                break;
            case 'u': // up half a page
                if (top > (height - 1) / 2) {
                    top -= (height - 1) / 2;
                } else {
                    top = 0;
                }
                break;
            case 'j': // down one line
            case '\n':
                top++;
                break;
            case 'k': // up one line
                if (top > 0) {
                    top--;
                }
                break;
            case 'g': // goto top page
                top = 0;
                break;
            case 'G': // goto bottom page
                buffer_load_all(buf);
                if (buf->size >= height - 1) {
                    top = buf->size - (height - 1);
                } else {
                    top = 0;
                }
                break;
            case 'q': // quit
                return;
        }

        if (!buffer_ensure(buf, top) && top > 0) {
            top--;
        }
    }
}

static void restore_terminal(void) {
    tcsetattr(tty_fd, TCSANOW, &old_termios);
}

static void usage(void) {
    fprintf(stderr, "usage: more FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd < 0) {
        err(EXIT_FAILURE, "open");
    }

    struct winsize winsz;
    if (tcgetwinsize(tty_fd, &winsz) < 0) {
        err(EXIT_FAILURE, "tcgetwinsize");
    }

    height = winsz.ws_row < 2 ? 2 : winsz.ws_row;
    width = winsz.ws_col < 1 ? 80 : winsz.ws_col;

    tcgetattr(tty_fd, &old_termios);
    atexit(restore_terminal);

    struct termios raw = old_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(tty_fd, TCSANOW, &raw);

    FILE* fp;

    if (argc == 0) {
        fp = stdin;
    } else {
        fp = fopen(argv[0], "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, "cannot open '%s'", argv[0]);
        }
    }

    struct page_buffer buf = {0};
    buf.fp = fp;

    page_content(&buf);

    buffer_free(&buf);
    return EXIT_SUCCESS;
}
