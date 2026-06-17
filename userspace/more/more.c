#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct page_buffer {
    char** lines;
    size_t capacity;
    size_t size;

    const char* filename;
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

static bool buffer_ensure(struct page_buffer* buf, size_t index) {
    if (index < buf->size) {
        return true;
    }

    if (buf->eof) {
        return false;
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
    return true;
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

static int page_content(struct page_buffer* buf) {
    if (!isatty(STDOUT_FILENO)) {
        char buffer[1024];

        ssize_t nread;
        while ((nread = read(fileno(buf->fp), buffer, sizeof(buffer))) > 0) {
            if (write(STDOUT_FILENO, buffer, nread) != nread) {
                err(EXIT_FAILURE, "write(stdout)");
            }
        }

        if (nread < 0) {
            warn("read(%s)", buf->filename);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    int ret = EXIT_SUCCESS;

    size_t top = 0;

    for (;;) {
        printf("\033[H\033[J");

        buffer_ensure(buf, top + (height - 1));

        for (size_t i = 0; i < height - 1; i++) {
            size_t idx = top + i;
            if (idx >= buf->size) {
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
            warn("read");
            ret = EXIT_FAILURE;
            break;
        }

        printf("\r\033[K");
        fflush(stdout);

        switch (c) {
            case ' ': // down a page
                top += height - 1;
                break;
            case '\n': // down a line
                top++;
                break;
            case 'f': // down a page
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
            case 'Q':
                return EXIT_SUCCESS;
        }

        if (!buffer_ensure(buf, top) && top > 0) {
            top--;
        }
    }

    return ret;
}

static void restore_terminal(void) {
    tcsetattr(tty_fd, TCSANOW, &old_termios);
}

static void usage(void) {
    fprintf(stderr, "usage: more FILE...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (isatty(STDIN_FILENO)) {
        tty_fd = STDIN_FILENO;
    } else {
        tty_fd = open(_PATH_TTY, O_RDONLY);
        if (tty_fd < 0) {
            err(EXIT_FAILURE, "open(%s)", _PATH_TTY);
        }
    }

    struct winsize winsz;
    if (ioctl(tty_fd, TIOCGWINSZ, &winsz) < 0) {
        err(EXIT_FAILURE, "ioctl");
    }

    height = winsz.ws_row < 2 ? 2 : winsz.ws_row;
    width = winsz.ws_col < 1 ? 80 : winsz.ws_col;

    tcgetattr(tty_fd, &old_termios);
    atexit(restore_terminal);

    struct termios raw = old_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(tty_fd, TCSANOW, &raw);

    char* filename;
    FILE* fp;

    if (argv[0] == NULL || strcmp(argv[0], "-") == 0) {
        filename = "stdin";
        fp = stdin;
    } else {
        filename = argv[0];
        fp = fopen(filename, "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, "cannot open '%s'", filename);
        }
    }

    struct page_buffer buf = {};
    buf.filename = filename;
    buf.fp = fp;

    int ret = page_content(&buf);

    buffer_free(&buf);

    return ret;
}
