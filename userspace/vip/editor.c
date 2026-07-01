#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"
#include "motions.h"

const struct row EMPTY_ROW = { .buf = "", .length = 0 };

static inline void* xmalloc(size_t size) {
    void* ret = malloc(size);
    if (!ret) {
        errx(EXIT_FAILURE, "malloc");
    }
    return ret;
}

static inline void* xrealloc(void* ptr, size_t new_size) {
    void* ret = realloc(ptr, new_size);
    if (!ret) {
        errx(EXIT_FAILURE, "realloc");
    }
    return ret;
}

static inline char* xstrndup(const char* str, size_t n) {
    char* ret = strndup(str, n);
    if (!ret) {
        err(EXIT_FAILURE, "strndup");
    }
    return ret;
}

bool editor_create(struct editor_state* state, const char* filename) {
    memset(state, 0, sizeof(struct editor_state));

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &state->winsize) < 0) {
        err(EXIT_FAILURE, "ioctl(TIOCGWINSZ)");
    }

    if (filename) {
        state->filename = strdup(filename);

        FILE* fp = fopen(filename, "r");
        if (!fp) {
            if (errno != ENOENT) {
                warn("%s", filename);
                return false;
            }
        } else {
            struct stat stat;
            if (fstat(fileno(fp), &stat) < 0) {
                warn("failed to stat %s", filename);
                return false;
            }

            if (!S_ISREG(stat.st_mode)) {
                warnx("%s is not a regular file", filename);
                return false;
            }

            char* line = NULL;
            size_t linecap = 0;
            ssize_t len;

            while ((len = getline(&line, &linecap, fp)) != -1) {
                if (state->row_count == state->row_capacity) {
                    size_t new_capacity = state->row_capacity ? state->row_capacity * 2 : 16;

                    struct row* new_rows = xrealloc(state->rows, new_capacity * sizeof(struct row));
                    state->rows = new_rows;
                    state->row_capacity = new_capacity;
                }

                size_t rowlen = (len > 0 && line[len - 1] == '\n') ? len - 1 : len;

                state->rows[state->row_count].buf = xmalloc(rowlen + 1);

                memcpy(state->rows[state->row_count].buf, line, rowlen);
                state->rows[state->row_count].buf[rowlen] = '\0';
                state->rows[state->row_count].length = rowlen;

                state->row_count++;
            }

            free(line);
            fclose(fp);
        }
    }

    if (state->row_count == 0) {
        editor_insert_row(state, &EMPTY_ROW, 0);
    }

    editor_set_mode(state, MODE_NORMAL);
    return true;
}

void editor_destroy(struct editor_state* state) {
    if (state->filename) {
        free(state->filename);
    }

    for (size_t i = 0; i < state->row_count; i++) {
        free(state->rows[i].buf);
    }
    free(state->rows);

    if (state->yank_buf) {
        free(state->yank_buf);
    }
}

bool editor_save(struct editor_state* state) {
    if (!state->filename) {
        return false;
    }

    FILE* fp = fopen(state->filename, "w");
    if (!fp) {
        return -1;
    }

    for (size_t i = 0; i < state->row_count; i++) {
        fputs(state->rows[i].buf, fp);
        fputc('\n', fp);
    }

    fclose(fp);
    return true;
}

bool editor_delete_char(struct editor_state* state, bool after) {
    size_t cy = state->cursor.y;
    if (cy >= state->row_count) {
        return false;
    }

    size_t cx = state->cursor.x;
    if (!after && cx == 0 && cy == 0) {
        return false;
    }

    struct row* row = &state->rows[cy];

    if (!after && cx == 0) {
        struct row* prev = &state->rows[cy - 1];

        size_t old_length = prev->length;

        prev->buf = xrealloc(prev->buf, prev->length + row->length + 1);
        memcpy(prev->buf + prev->length, row->buf, row->length + 1);

        prev->length += row->length;

        editor_delete_row(state, cy);

        state->cursor.y--;
        state->cursor.x = old_length;
        return true;
    }

    size_t pos;

    if (after) {
        if (row->length == 0) {
            return false;
        }

        if (cx >= row->length) {
            pos = row->length - 1;
        } else {
            pos = cx;
        }
    } else {
        if (cx == 0) {
            return false;
        }

        pos = MIN(cx, row->length) - 1;
    }

    memmove(&row->buf[pos], &row->buf[pos + 1], row->length - pos);
    row->length--;

    row->buf = xrealloc(row->buf, row->length + 1);

    if (!after) {
        state->cursor.x--;
    }

    return true;
}

bool editor_delete_range(struct editor_state* state, const struct range* range) {
    if (range->y >= state->row_count) {
        return false;
    }

    if (range->line) {
        if (!editor_delete_row(state, range->y)) {
            return false;
        }

        if (state->row_count == 0) {
            if (!editor_insert_row(state, &EMPTY_ROW, 0)) {
                return false;
            }
        }

        if (state->cursor.y >= state->row_count) {
            state->cursor.y = state->row_count - 1;
        }

        state->cursor.x = 0;

        return true;
    }

    struct row* row = &state->rows[range->y];
    if (range->start_x > range->end_x) {
        return false;
    }

    if (range->end_x > row->length) {
        return false;
    }

    size_t len = range->end_x - range->start_x;
    if (len == 0) {
        return true;
    }

    memmove(&row->buf[range->start_x], &row->buf[range->end_x], row->length - range->end_x + 1);
    row->length -= len;

    row->buf = xrealloc(row->buf, row->length + 1);

    state->cursor.y = range->y;
    state->cursor.x = range->start_x;

    return true;
}

bool editor_delete_row(struct editor_state* state, size_t at) {
    if (at >= state->row_count) {
        return false;
    }

    free(state->rows[at].buf);

    memmove(&state->rows[at], &state->rows[at + 1], (state->row_count - at - 1) * sizeof(struct row));
    state->row_count--;

    return true;
}

bool editor_insert_char(struct editor_state* state, char c) {
    size_t cy = state->cursor.y;
    if (cy >= state->row_count) {
        return false;
    }

    struct row* row = &state->rows[cy];

    size_t cx = MIN(state->cursor.x, row->length);

    row->buf = xrealloc(row->buf, row->length + 2);

    memmove(&row->buf[cx + 1], &row->buf[cx], row->length - cx + 1);

    row->buf[cx] = c;
    row->length++;

    state->cursor.x++;
    return true;
}

bool editor_insert_newline(struct editor_state* state) {
    size_t cy = state->cursor.y;
    if (cy >= state->row_count) {
        return false;
    }

    struct row* row = &state->rows[cy];

    size_t cx = MIN(state->cursor.x, row->length);

    char* left = xstrndup(row->buf, cx);
    char* right = row->buf + cx;

    struct row new_row = {
        .buf = right,
        .length = strlen(right),
    };

    if (!editor_insert_row(state, &new_row, cy + 1)) {
        free(left);
        return false;
    }

    free(row->buf);
    row->buf = left;
    row->length = cx;

    state->cursor.x = 0;
    state->cursor.y++;

    return true;
}

bool editor_insert_row(struct editor_state* state, const struct row* row, size_t at) {
    if (at > state->row_count) {
        return false;
    }

    state->rows = xrealloc(state->rows, (state->row_count + 1) * sizeof(struct row));

    if (at != state->row_count) {
        memmove(&state->rows[at + 1], &state->rows[at], (state->row_count - at) * sizeof(struct row));
    }

    state->rows[at].buf = xstrndup(row->buf, row->length);
    state->rows[at].length = row->length;

    state->row_count++;

    return true;
}

bool editor_join_lines(struct editor_state *state) {
    if (state->cursor.y >= state->row_count - 1) {
        return false;
    }

    struct row* row = &state->rows[state->cursor.y];
    struct row* next = &state->rows[state->cursor.y + 1];

    bool need_space = row->length > 0 && !isspace((unsigned char) row->buf[row->length - 1]) &&
        next->length > 0 && !isspace((unsigned char) next->buf[0]);

    size_t newlen = row->length + next->length + need_space;

    row->buf = xrealloc(row->buf, newlen + 1);

    if (need_space) {
        row->buf[row->length] = ' ';
        memcpy(row->buf + row->length + 1, next->buf, next->length);
    } else {
        memcpy(row->buf + row->length, next->buf, next->length);
    }

    row->length = newlen;
    row->buf[newlen] = '\0';

    free(next->buf);

    memmove(&state->rows[state->cursor.y + 1], &state->rows[state->cursor.y + 2], sizeof(struct row) * (state->row_count - state->cursor.y - 2));
    state->row_count--;

    return true;
}

bool editor_paste(struct editor_state* state) {
    if (!state->yank_buf || state->yank_length == 0) {
        return false;
    }

    if (state->yank_buf[state->yank_length - 1] == '\n') {
        struct row new_row = {
            .buf = state->yank_buf,
            .length = state->yank_length - 1,
        };

        if (!editor_insert_row(state, &new_row, state->cursor.y + 1)) {
            return false;
        }

        state->cursor.y++;
        state->cursor.x = 0;
        return true;
    }

    for (size_t i = 0; i < state->yank_length; i++) {
        editor_insert_char(state, state->yank_buf[i]);
    }

    return true;
}

bool editor_replace_char(struct editor_state* state, char c) {
    if (!isprint(c)) {
        return false;
    }

    size_t cy = state->cursor.y;
    if (cy >= state->row_count) {
        return false;
    }

    struct row* row = &state->rows[cy];

    size_t cx = state->cursor.x;
    if (cx >= row->length) {
        return false;
    }

    row->buf[cx] = c;
    return true;
}

void editor_scroll(struct editor_state* state) {
    if (state->cursor.y < state->row_offset + SCROLLOFF) {
        if (state->cursor.y < SCROLLOFF) {
            state->row_offset = 0;
        } else {
            state->row_offset = state->cursor.y - SCROLLOFF;
        }
    }

    if (state->cursor.y >= state->row_offset + state->winsize.ws_row - SCROLLOFF) {
        state->row_offset = state->cursor.y - state->winsize.ws_row + SCROLLOFF + 1;
    }

    if (state->cursor.x < state->col_offset + SCROLLOFF) {
        if (state->cursor.x < SCROLLOFF) {
            state->col_offset = 0;
        } else {
            state->col_offset = state->cursor.x - SCROLLOFF;
        }
    }

    if (state->cursor.x >= state->col_offset + state->winsize.ws_col - SCROLLOFF) {
        state->col_offset = state->cursor.x - state->winsize.ws_col + SCROLLOFF + 1;
    }

    if (state->cursor.y >= state->row_count) {
        state->cursor.y = state->row_count - 1;
    }

    state->cursor.x = MIN(state->cursor.x, state->rows[state->cursor.y].length);
}

void editor_set_mode(struct editor_state* state, enum mode mode) {
    switch (mode) {
        case MODE_NORMAL:
            if (state->mode == MODE_INSERT) {
                motion_left(state);
            }

            write(STDOUT_FILENO, "\033[2 q", 5); // block cursor
            break;
        case MODE_INSERT:
            write(STDOUT_FILENO, "\033[6 q", 5); // line cursor
            break;
        case MODE_COMMAND:
            state->command_buf[0] = '\0';
            state->command_length = 0;
            break;
        default:
            __builtin_unreachable();
    }

    state->mode = mode;
}

bool editor_yank_range(struct editor_state* state, const struct range* range) {
    if (range->y >= state->row_count) {
        return false;
    }

    struct row* row = &state->rows[range->y];

    if (range->line) {
        state->yank_buf = xmalloc(row->length + 2);

        memcpy(state->yank_buf, row->buf, row->length);
        state->yank_buf[row->length] = '\n';
        state->yank_buf[row->length + 1] = '\0';

        state->yank_length = row->length + 1;

        return true;
    }

    if (range->start_x > range->end_x || range->end_x > row->length) {
        return false;
    }

    size_t len = range->end_x - range->start_x;

    free(state->yank_buf);

    state->yank_buf = xmalloc(len + 1);

    memcpy(state->yank_buf, row->buf + range->start_x, len);
    state->yank_buf[len] = '\0';

    state->yank_length = len;

    return true;
}
