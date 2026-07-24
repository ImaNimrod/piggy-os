#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "history.h"
#include "interactive.h"

#define CTRL(c) ((c) & 0x1f)

enum key {
    KEY_NONE,
    KEY_CHAR,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

static enum key read_key(char* ch) {
    unsigned char c;

    ssize_t nread;
    while ((nread = read(STDIN_FILENO, &c, 1)) == 0);

    if (nread < 0) {
        return KEY_NONE;
    }

    if (c != '\033') {
        *ch = c;
        return KEY_CHAR;
    }

    unsigned char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
        return KEY_NONE;
    }

    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
        return KEY_NONE;
    }

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                return KEY_NONE;
            }

            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DELETE;
                    case '4': return KEY_END;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
        } else {
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }

    return KEY_NONE;
}

static void redraw(const char* buf, size_t len, size_t cursor) {
    write(STDOUT_FILENO, "\r", 1);
    int prompt_len = dprintf(STDOUT_FILENO, "\033[94msh\033[0m:\033[32m%s\033[0m> ", pwd);
    write(STDOUT_FILENO, buf, len);

    write(STDOUT_FILENO, "\033[K", 3);
    write(STDOUT_FILENO, "\r", 1);

    dprintf(STDOUT_FILENO, "\033[%dC", prompt_len - 18);

    if (cursor) {
        dprintf(STDOUT_FILENO, "\033[%zuC", cursor);
    }
}

ssize_t readline_interactive(char** out) {
    size_t cap = 64;
    size_t len = 0;
    size_t cursor = 0;

    char* buf = malloc(cap);
    if (!buf) {
        errx(EXIT_FAILURE, "malloc");
    }
    buf[0] = '\0';

    redraw(buf, len, cursor);

    for (;;) {
        char ch;
        enum key key = read_key(&ch);

        switch (key) {
            case KEY_NONE:
                continue;
            case KEY_CHAR:
                switch (ch) {
                    case '\r':
                    case '\n':
                        write(STDOUT_FILENO, "\r\n", 2);
                        *out = buf;
                        return len;
                    case CTRL('A'):
                        cursor = 0;
                        redraw(buf, len, cursor);
                        break;
                    case CTRL('E'):
                        cursor = len;
                        redraw(buf, len, cursor);
                        break;
                    case CTRL('B'):
                        if (cursor > 0) {
                            cursor--;
                            redraw(buf, len, cursor);
                        }
                        break;
                    case CTRL('F'):
                        if (cursor < len) {
                            cursor++;
                            redraw(buf, len, cursor);
                        }
                        break;
                    case CTRL('C'):
                        write(STDOUT_FILENO, "^C\r\n", 4);
                        buf[0] = '\0';
                        *out = buf;
                        return 0;
                    case CTRL('L'):
                        write(STDOUT_FILENO, "\033[2J\033[H", 7);
                        redraw(buf, len, cursor);
                        break;
                    case 127:
                        if (cursor == 0) {
                            break;
                        }

                        memmove(buf + cursor - 1, buf + cursor, len - cursor + 1);
                        cursor--;
                        len--;

                        redraw(buf, len, cursor);
                        break;
                    default:
                        if (!isprint(ch)) {
                            break;
                        }

                        if (len + 1 >= cap) {
                            cap *= 2;

                            char* new = realloc(buf, cap);
                            if (!new) {
                                free(buf);
                                errx(EXIT_FAILURE, "realloc");
                            }

                            buf = new;
                        }

                        memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
                        buf[cursor++] = ch;
                        len++;

                        redraw(buf, len, cursor);
                        break;
                }
                break;
            case KEY_UP: {
                const char* line = history_prev();
                if (line) {
                    strcpy(buf, line);
                    len = cursor = strlen(buf);
                    redraw(buf, len, cursor);
                }
                break;
            }
            case KEY_DOWN: {
                const char* line = history_next();
                if (line) {
                    strcpy(buf, line);
                    len = cursor = strlen(buf);
                    redraw(buf, len, cursor);
                }
                break;
            }
            case KEY_LEFT:
                if (cursor > 0) {
                    cursor--;
                    redraw(buf, len, cursor);
                }
                break;
            case KEY_RIGHT:
                if (cursor < len) {
                    cursor++;
                    redraw(buf, len, cursor);
                }
                break;
            case KEY_HOME:
                cursor = 0;
                redraw(buf, len, cursor);
                break;
            case KEY_END:
                cursor = len;
                redraw(buf, len, cursor);
                break;
            case KEY_DELETE:
                if (cursor >= len) {
                    break;
                }

                memmove(buf + cursor, buf + cursor + 1, len - cursor);
                buf[--len] = '\0';

                redraw(buf, len, cursor);
                break;
        }
    }
}
