#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios old_termios;
static unsigned short height, width;

static void page_content(FILE* fp) {
    int lines = 0;
    char line[width + 2];

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strlen(line) == (size_t) (width + 1) && line[width] != '\n') {
            line[width - 1] = '+';
            line[width]     = '\n';
            line[width + 1] = '\0';
        }

        fputs(line, stdout);
        lines++;

        if (lines == height - 1) {
            printf("--more--");
            fflush(stdout);

            int c = getchar();
            printf("\r\033[K");

            if (c == 'q' || c == EOF) {
                exit(EXIT_SUCCESS);
            } else if (c == '\n') {
                lines = height - 2;
            } else if (c == ' ') {
                lines = 0;
            }
        }
    }
}

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
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

    struct winsize winsz;
    if (tcgetwinsize(STDIN_FILENO, &winsz) < 0) {
        err(EXIT_FAILURE, "tcgetwinsiz");
    }

    height = winsz.ws_row < 2 ? 2 : winsz.ws_row;
    width = winsz.ws_col < 1 ? 80 : winsz.ws_col;

    tcgetattr(STDIN_FILENO, &old_termios);
    atexit(restore_terminal);

    struct termios raw = old_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    FILE* fp;

    if (argc < 0) {
        fp = stdin;
    } else {
        fp = fopen(argv[0], "r");
        if (fp == NULL) {
            err(EXIT_FAILURE, "cannot open '%s'", argv[0]);
        }
    }

    page_content(fp);

    if (fp != stdin) {
        fclose(fp);
    }
    return EXIT_SUCCESS;
}
