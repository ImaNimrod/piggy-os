#ifndef _VIP_EDITOR_H
#define _VIP_EDITOR_H

#include <stddef.h>
#include <termios.h>

#define SCROLLOFF 8
#define TAB_WIDTH 4

enum mode {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
};

enum operator {
    OP_NONE,
    OP_CHANGE,
    OP_DELETE,
    OP_FINDB,
    OP_FINDF,
    OP_GOTO,
    OP_REPLACE,
    OP_YANK,
};

struct range {
    size_t start_x;
    size_t end_x;
    size_t y;
    bool line;
};

struct row {
    char* buf;
    size_t length;
};

struct editor_state {
    bool quit;
    enum mode mode;
    enum operator operator;

    char* filename;

    struct row* rows;
    size_t row_capacity;
    size_t row_count;

    size_t row_offset;
    size_t col_offset;

    char* yank_buf;
    size_t yank_length;

    char command_buf[256];
    size_t command_length;

    struct {
        size_t y;
        size_t x;
    } cursor;

    struct winsize winsize;
};

extern const struct row EMPTY_ROW;

bool editor_create(struct editor_state* state, const char* filename);
void editor_destroy(struct editor_state* state);
bool editor_save(struct editor_state* state);

bool editor_delete_char(struct editor_state* state, bool after);
bool editor_delete_range(struct editor_state* state, const struct range* range);
bool editor_delete_row(struct editor_state* state, size_t at);

bool editor_insert_char(struct editor_state* state, char c);
bool editor_insert_newline(struct editor_state* state);
bool editor_insert_row(struct editor_state* state, const struct row* row, size_t at);

bool editor_join_lines(struct editor_state *state);

bool editor_paste(struct editor_state* state);
bool editor_replace_char(struct editor_state* state, char c);
void editor_scroll(struct editor_state* state);
void editor_set_mode(struct editor_state* state, enum mode mode);
bool editor_yank_range(struct editor_state* state, const struct range* range);

#endif /* _VIP_EDITOR_H */
