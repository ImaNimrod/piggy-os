#include <sys/ioctl.h>
#include <sys/param.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "editor.h"
#include "motions.h"

#define CTRL(c) ((c) & 0x1f)

struct abuf {
    char* buf;
    size_t length;
    size_t capacity;
};

static struct termios old_termios;
static struct editor_state state;

static inline const char* mode_to_string(enum mode mode) {
    switch (mode) {
        case MODE_NORMAL:
            return "NORMAL";
        case MODE_INSERT:
            return "INSERT";
        case MODE_COMMAND:
            return "COMMAND";
        default:
            __builtin_unreachable();
    }
}

static void ab_append(struct abuf* ab, const char* s, size_t length) {
    if (ab->length + length >= ab->capacity) {
        size_t new_capacity = ab->capacity ? ab->capacity : 32;

        while (new_capacity < ab->length + length) {
            new_capacity *= 2;
        }

        char* new_buf = realloc(ab->buf, new_capacity);
        if (!new_buf) {
            errx(EXIT_FAILURE, "realloc");
        }

        ab->buf = new_buf;
        ab->capacity = new_capacity;
    }

    memcpy(ab->buf + ab->length, s, length);
    ab->length += length;
}

static void ab_free(struct abuf* ab) {
    free(ab->buf);
}

static void draw_status_bar(struct editor_state* state, struct abuf* out) {
    const char* filename;
    if (state->filename) {
        char* basename = strrchr(state->filename, '/');
        filename = basename ? basename + 1 : state->filename;
    } else {
        filename = "[No Name]";
    }

    char left[256];
    int left_length = snprintf(left, sizeof(left), " %s | %s ",
            mode_to_string(state->mode), filename);

    char right[64];
    int right_length = snprintf(right, sizeof(right), "%zu:%zu",
            state->cursor.y + 1, state->cursor.x + 1);

    ab_append(out, "\033[7m", 4);

    char buf[32];
    int length = snprintf(buf, sizeof(buf), "\033[%d;1H", state->winsize.ws_row - 1);
    ab_append(out, buf, length);

    ab_append(out, left, left_length);

    size_t padding = state->winsize.ws_col - left_length - right_length;
    while (padding--) {
        ab_append(out, " ", 1);
    }

    ab_append(out, right, right_length);

    ab_append(out, "\033[m", 3);

    length = snprintf(buf, sizeof(buf), "\033[%d;1H", state->winsize.ws_row);
    ab_append(out, buf, length);

    ab_append(out, "\033[K", 3);

    if (state->mode == MODE_COMMAND) {
        ab_append(out, ":", 1);
        ab_append(out, state->command_buf, state->command_length);
    }
}

static bool execute_command(struct editor_state* state) {
    char filename[PATH_MAX];
    if (sscanf(state->command_buf, "e %255s", filename) == 1) {
        editor_destroy(state);
        editor_create(state, filename);
        return true;
    }

    for (size_t i = 0; i < state->command_length; i++) {
        switch (state->command_buf[i]) {
            case 'q':
                state->quit = true;
                break;
            case 'w':
                if (!editor_save(state)) {
                    return false;
                }
                break;
        }
    }

    return true;
}

static size_t gutter_width(struct editor_state* state) {
    size_t ret = 1;

    size_t lines = state->row_count;
    while (lines >= 10) {
        ret++;
        lines /= 10;
    }

    return ret + 1;
}

static bool handle_key(struct editor_state* state) {
    char c = '\0';

    ssize_t nread;
    while ((nread = read(STDIN_FILENO, &c, sizeof(char))) == 0) {}

    if (nread == -1) {
        return false;
    }

    switch (state->mode) {
        case MODE_NORMAL:
            if (state->operator != OP_NONE) {
                struct range range;

                switch (state->operator) {
                    case OP_CHANGE:
                        switch (c) {
                            case 'b':
                                if (motion_to_range(state, MOTION_WORDB, &range)) {
                                    editor_delete_range(state, &range);
                                    editor_set_mode(state, MODE_INSERT);
                                }
                                break;
                            case 'c':
                                if (motion_to_range(state, MOTION_LINE, &range)) {
                                    editor_delete_range(state, &range);
                                    editor_set_mode(state, MODE_INSERT);
                                }
                                break;
                            case 'e':
                                if (motion_to_range(state, MOTION_WORDE, &range)) {
                                    editor_delete_range(state, &range);
                                    editor_set_mode(state, MODE_INSERT);
                                }
                                break;
                            case 'w':
                                if (motion_to_range(state, MOTION_WORDF, &range)) {
                                    editor_delete_range(state, &range);
                                    editor_set_mode(state, MODE_INSERT);
                                }
                                break;
                            case '$':
                                if (motion_to_range(state, MOTION_LINE_END, &range)) {
                                    editor_delete_range(state, &range);
                                    editor_set_mode(state, MODE_INSERT);
                                }
                                break;
                        }
                        break;
                    case OP_DELETE:
                        switch (c) {
                            case 'b':
                                if (motion_to_range(state, MOTION_WORDB, &range)) {
                                    editor_delete_range(state, &range);
                                }
                                break;
                            case 'd':
                                if (motion_to_range(state, MOTION_LINE, &range)) {
                                    editor_delete_range(state, &range);
                                }
                                break;
                            case 'e':
                                if (motion_to_range(state, MOTION_WORDE, &range)) {
                                    editor_delete_range(state, &range);
                                }
                                break;
                            case 'w':
                                if (motion_to_range(state, MOTION_WORDF, &range)) {
                                    editor_delete_range(state, &range);
                                }
                                break;
                            case '$':
                                if (motion_to_range(state, MOTION_LINE_END, &range)) {
                                    editor_delete_range(state, &range);
                                }
                                break;
                        }
                        break;
                    case OP_FINDB:
                        if (isprint(c)) {
                            motion_find_backward(state, c);
                        }
                        break;
                    case OP_FINDF:
                        if (isprint(c)) {
                            motion_find_forward(state, c);
                        }
                        break;
                    case OP_GOTO:
                        switch (c) {
                            case 'g':
                                motion_buffer_top(state);
                                break;
                            case '0':
                                motion_line_start(state);
                                break;
                            case '$':
                                motion_line_end(state);
                                break;
                            case '_':
                                motion_line_last_nonwhitespace(state);
                                break;
                        }
                        break;
                    case OP_REPLACE:
                        editor_replace_char(state, c);
                        break;
                    case OP_YANK:
                        switch (c) {
                            case 'b':
                                if (motion_to_range(state, MOTION_WORDB, &range)) {
                                    editor_yank_range(state, &range);
                                }
                                break;
                            case 'e':
                                if (motion_to_range(state, MOTION_WORDE, &range)) {
                                    editor_yank_range(state, &range);
                                }
                                break;
                            case 'w':
                                if (motion_to_range(state, MOTION_WORDF, &range)) {
                                    editor_yank_range(state, &range);
                                }
                                break;
                            case 'y':
                                if (motion_to_range(state, MOTION_LINE, &range)) {
                                    editor_yank_range(state, &range);
                                }
                                break;
                            case '$':
                                if (motion_to_range(state, MOTION_LINE_END, &range)) {
                                    editor_yank_range(state, &range);
                                }
                                break;
                        }
                        break;
                    default:
                        __builtin_unreachable();
                }

                state->operator = OP_NONE;
                return true;
            }

            switch (c) {
                case 'c':
                    state->operator = OP_CHANGE;
                    break;
                case 'd':
                    state->operator = OP_DELETE;
                    break;
                case 'F':
                    state->operator = OP_FINDB;
                    break;
                case 'f':
                    state->operator = OP_FINDF;
                    break;
                case 'g':
                    state->operator = OP_GOTO;
                    break;
                case 'r':
                    state->operator = OP_REPLACE;
                    break;
                case 'y':
                    state->operator = OP_YANK;
                    break;
                case 'h':
                    motion_left(state);
                    break;
                case 'l':
                    motion_right(state);
                    break;
                case 'k':
                    motion_up(state);
                    break;
                case 'j':
                    motion_down(state);
                    break;
                case '0':
                    motion_line_start(state);
                    break;
                case '$':
                    motion_line_end(state);
                    break;
                case 'G':
                    motion_buffer_bottom(state);
                    break;
                case CTRL('f'):
                    motion_page_down(state, false);
                    break;
                case CTRL('b'):
                    motion_page_up(state, false);
                    break;
                case CTRL('d'):
                    motion_page_down(state, true);
                    break;
                case CTRL('u'):
                    motion_page_up(state, true);
                    break;
                case 'b':
                    motion_word_backward(state);
                    break;
                case 'e':
                    motion_word_end(state);
                    break;
                case 'w':
                    motion_word_forward(state);
                    break;
                case 'a':
                    motion_right(state);
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'A':
                    motion_line_end(state);
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'i':
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'I':
                    motion_line_start(state);
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'o':
                    editor_insert_row(state, &EMPTY_ROW, state->cursor.y + 1);
                    motion_line_start(state);
                    motion_down(state);
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'O':
                    editor_insert_row(state, &EMPTY_ROW, state->cursor.y);
                    motion_line_start(state);
                    editor_set_mode(state, MODE_INSERT);
                    break;
                case 'J':
                    editor_join_lines(state);
                    break;
                case 'p':
                    editor_paste(state);
                    break;
                case 'x':
                    editor_delete_char(state, true);
                    break;
                case ':':
                    editor_set_mode(state, MODE_COMMAND);
                    break;
            }

            break;
        case MODE_INSERT:
            switch (c) {
                case '\033':
                    editor_set_mode(state, MODE_NORMAL);
                    break;
                case '\r':
                    editor_insert_newline(state);
                    break;
                case '\t':
                    size_t n = TAB_WIDTH - (state->cursor.x % TAB_WIDTH);
                    for (size_t i = 0; i < n; i++) {
                        editor_insert_char(state, ' ');
                    }
                    break;
                case 127:
                    editor_delete_char(state, false);
                    break;
                default:
                    editor_insert_char(state, c);
                    break;
            }

            break;
        case MODE_COMMAND:
            switch (c) {
                case '\033':
                    editor_set_mode(state, MODE_NORMAL);
                    break;
                case '\r':
                    execute_command(state);
                    editor_set_mode(state, MODE_NORMAL);
                    break;
                case 127:
                    if (state->command_length >= 1) {
                        state->command_length--;
                        state->command_buf[state->command_length] = '\0';
                    }
                    break;
                default:
                    if (isprint(c) && state->command_length < (sizeof(state->command_buf) - 1)) {
                        state->command_buf[state->command_length++] = c;
                    }
            }
            break;
        default:
            __builtin_unreachable();
    }

    return true;
}

static void update(struct editor_state* state) {
    editor_scroll(state);

    struct abuf out = {};

    ab_append(&out, "\033[?25l", 6);
    ab_append(&out, "\033[H", 3);

    size_t gutter = gutter_width(state);

    for (int y = 0; y < state->winsize.ws_row - 2; y++) {
        size_t filerow = y + state->row_offset;

        if (filerow < state->row_count) {
            char lbuf[32];

            if (filerow == state->cursor.y) {
                ab_append(&out, "\033[93m", 5); // yellow
            } else {
                ab_append(&out, "\033[90m", 5); // gray
            }

            int length = snprintf(lbuf, sizeof(lbuf), " %*zu ", (int) (gutter - 1), filerow + 1);
            ab_append(&out, lbuf, length);

            ab_append(&out, "\033[39m", 5);

            struct row* row = &state->rows[filerow];

            if (state->col_offset < row->length) {
                size_t len = MIN(row->length - state->col_offset, state->winsize.ws_col - gutter);
                ab_append(&out, row->buf + state->col_offset, len);
            }
        } else {
            char lbuf[32];
            int length = snprintf(lbuf, sizeof(lbuf), "%*s ", (int)(gutter - 1), "~");
            ab_append(&out, lbuf, length);
        }

        ab_append(&out, "\033[K", 3);

        if (y < state->winsize.ws_row - 1) {
            ab_append(&out, "\r\n", 2);
        }
    }

    draw_status_bar(state, &out);

    int screen_y = state->cursor.y - state->row_offset;
    int screen_x = state->cursor.x - state->col_offset + gutter + 1;

    if (screen_y < 0) {
        screen_y = 0;
    }
    if (screen_x < 0) {
        screen_x = 0;
    }
    if (screen_y >= state->winsize.ws_row - 2) {
        screen_y = state->winsize.ws_row - 3;
    }
    if (screen_x >= state->winsize.ws_col) {
        screen_x = state->winsize.ws_col - 1;
    }

    char buf[32];
    int length = snprintf(buf, sizeof(buf), "\033[%d;%dH", screen_y + 1, screen_x + 1);

    ab_append(&out, buf, length);

    ab_append(&out, "\033[?25h", 6);

    write(STDOUT_FILENO, out.buf, out.length);
    ab_free(&out);
}

static void restore_termios(void) {
    char buf[32];
    int length = snprintf(buf, sizeof(buf), "\033[%d;1H", state.winsize.ws_row);

    write(STDOUT_FILENO, buf, length);
    write(STDOUT_FILENO, "\033[K\r\n", 5);

    write(STDOUT_FILENO, "\033[?25h", 6);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

static void usage(void) {
    fprintf(stderr, "usage: vip [FILE]...\n");
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc > 1) {
        errx(EXIT_FAILURE, "extra operands");
    }

    if (!isatty(STDIN_FILENO)) {
        err(EXIT_FAILURE, "must be run in terminal");
    }

    if (tcgetattr(STDIN_FILENO, &old_termios) < 0) {
        err(EXIT_FAILURE, "tcgetattr");
    }

    struct termios new_termios = old_termios;
    new_termios.c_iflag &= ~(BRKINT | ICRNL | ISTRIP | IXON);
    new_termios.c_oflag &= ~(OPOST);
    new_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 1;

    atexit(restore_termios);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) < 0) {
        err(EXIT_FAILURE, "tcsetattr");
    }

    int ret = EXIT_SUCCESS;

    if (argc == 0) {
        if (!editor_create(&state, NULL)) {
            return EXIT_FAILURE;
        }

        while (!state.quit) {
            update(&state);
            handle_key(&state);
        }

        editor_destroy(&state);
    } else {
        for (int i = 0; i < argc; i++) {
            if (!editor_create(&state, argv[i])) {
                ret = EXIT_FAILURE;
                continue;
            }

            while (!state.quit) {
                update(&state);
                handle_key(&state);
            }

            editor_destroy(&state);
        }
    }

    return ret;
}
