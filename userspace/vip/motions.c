#include <sys/param.h>
#include <sys/types.h>

#include <ctype.h>
#include <string.h>

#include "motions.h"

enum char_class {
    CHAR_PUNCT,
    CHAR_SPACE,
    CHAR_WORD,
};

static enum char_class char_class(unsigned char c) {
    if (isspace(c)) {
        return CHAR_SPACE;
    }

    if (isalnum(c) || c == '_') {
        return CHAR_WORD;
    }

    return CHAR_PUNCT;
}

static size_t skip_run_forward(const struct row* row, size_t i) {
    enum char_class cls = char_class(row->buf[i]);

    while (i < row->length && char_class(row->buf[i]) == cls) {
        i++;
    }

    return i;
}

static size_t skip_run_backward(const struct row* row, size_t i) {
    enum char_class cls = char_class(row->buf[i]);

    while (i > 0 && char_class(row->buf[i - 1]) == cls) {
        i--;
    }

    return i;
}

static size_t skip_spaces_forward(const struct row* row, size_t i) {
    while (i < row->length && char_class(row->buf[i]) == CHAR_SPACE) {
        i++;
    }
    return i;
}

static size_t skip_spaces_backward(const struct row* row, size_t i) {
    while (i > 0 && char_class(row->buf[i]) == CHAR_SPACE) {
        i--;
    }
    return i;
}

void motion_left(struct editor_state* state) {
    if (state->cursor.x > 0) {
        state->cursor.x--;
    }
}

void motion_right(struct editor_state* state) {
    if (state->cursor.x < state->rows[state->cursor.y].length) {
        state->cursor.x++;
    }
}

void motion_up(struct editor_state* state) {
    if (state->cursor.y > 0) {
        state->cursor.y--;
    }
}

void motion_down(struct editor_state* state) {
    if (state->cursor.y + 1 < state->row_count) {
        state->cursor.y++;
    }
}

void motion_buffer_bottom(struct editor_state* state) {
    state->cursor.y = state->row_count - 1;
    state->cursor.x = MIN(state->cursor.x, state->rows[state->cursor.y].length);
}

void motion_buffer_top(struct editor_state* state) {
    state->cursor.y = 0;
    state->cursor.x = MIN(state->cursor.x, state->rows[state->cursor.y].length);
}

void motion_find_backward(struct editor_state* state, char c) {
    if (state->cursor.x == 0) {
        return;
    }

    struct row* row = &state->rows[state->cursor.y];
    if (row->length == 0) {
        return;
    }

    for (ssize_t i = state->cursor.x - 1; i >= 0; i--) {
        if (row->buf[i] == c) {
            state->cursor.x = i;
            return;
        }
    }
}

void motion_find_forward(struct editor_state* state, char c) {
    struct row* row = &state->rows[state->cursor.y];
    if (row->length == 0) {
        return;
    }

    for (size_t i = state->cursor.x + 1; i < row->length; i++) {
        if (row->buf[i] == c) {
            state->cursor.x = i;
            return;
        }
    }
}

void motion_line_start(struct editor_state* state) {
    state->cursor.x = 0;
}

void motion_line_end(struct editor_state* state) {
    struct row* row = &state->rows[state->cursor.y];
    if (row->length == 0) {
        state->cursor.x = 0;
    } else {
        state->cursor.x = row->length - 1;
    }
}

void motion_line_last_nonwhitespace(struct editor_state* state) {
    struct row* row = &state->rows[state->cursor.y];

    if (row->length == 0) {
        state->cursor.x = 0;
        return;
    }

    size_t x = row->length - 1;
    while (x > 0 && isspace((unsigned char) row->buf[x])) {
        x--;
    }

    state->cursor.x = x;
}

void motion_page_down(struct editor_state* state, bool half) {
    state->cursor.y = MIN(state->cursor.y + (state->winsize.ws_row / (half ? 2 : 1)), state->row_count - 1);
}

void motion_page_up(struct editor_state* state, bool half) {
    size_t n = state->winsize.ws_row / (half ? 2 : 1);
    if (state->cursor.y < n) {
        state->cursor.y = 0;
    } else {
        state->cursor.y -= n;
    }
}

void motion_word_backward(struct editor_state* state) {
    if (state->cursor.x == 0) {
        return;
    }

    struct row* row = &state->rows[state->cursor.y];

    size_t cx = state->cursor.x - 1;

    cx = skip_spaces_backward(row, cx);
    cx = skip_run_backward(row, cx);

    state->cursor.x = cx;
}

void motion_word_end(struct editor_state* state) {
    struct row* row = &state->rows[state->cursor.y];

    size_t cx = state->cursor.x;
    if (cx >= row->length) {
        return;
    }

    enum char_class cls = char_class(row->buf[cx]);

    if (cls == CHAR_SPACE) {
        cx = skip_spaces_forward(row, cx);
        if (cx >= row->length) {
            return;
        }
    } else {
        if (cx + 1 >= row->length ||
                char_class(row->buf[cx + 1]) != cls) {

            cx++;

            cx = skip_spaces_forward(row, cx);
            if (cx >= row->length) {
                return;
            }
        }
    }

    cx = skip_run_forward(row, cx);

    state->cursor.x = cx - 1;
}

void motion_word_forward(struct editor_state* state) {
    struct row* row = &state->rows[state->cursor.y];

    size_t cx = state->cursor.x;
    if (cx >= row->length) {
        return;
    }

    if (char_class(row->buf[cx]) != CHAR_SPACE) {
        cx = skip_run_forward(row, cx);
    }

    cx = skip_spaces_forward(row, cx);

    state->cursor.x = cx;
}

bool motion_to_range(struct editor_state* state, enum motion motion, struct range* range) {
    size_t cy = state->cursor.y;
    if (cy >= state->row_count) {
        return false;
    }

    struct row* row = &state->rows[cy];
    size_t cx = MIN(state->cursor.x, row->length);

    range->y = cy;
    range->line = motion == MOTION_LINE;

    switch (motion) {
        case MOTION_LINE:
            range->start_x = 0;
            range->end_x = row->length;
            return true;
        case MOTION_LINE_END:
            range->start_x = state->cursor.x;

            if (row->length == 0) {
                range->end_x = 0;
            } else {
                range->end_x = row->length - 1;
            }
            return true;
        case MOTION_WORDB: {
            if (cx == 0) {
                return false;
            }

            size_t i = cx - 1;

            i = skip_spaces_backward(row, i);
            i = skip_run_backward(row, i);

            range->start_x = i;
            range->end_x = cx;
            return i != cx;
        }
        case MOTION_WORDE: {
            size_t i = cx;

            i = skip_spaces_forward(row, i);

            if (i == row->length) {
                return false;
            }

            i = skip_run_forward(row, i);

            range->start_x = cx;
            range->end_x = i;
            return i != cx;
        }
        case MOTION_WORDF: {
            size_t i = cx;

            if (i == row->length) {
                return false;
            }

            if (char_class(row->buf[i]) != CHAR_SPACE) {
                i = skip_run_forward(row, i);
            }

            i = skip_spaces_forward(row, i);

            range->start_x = cx;
            range->end_x = i;
            return i != cx;
        }
        default:
            __builtin_unreachable();
    }
}
