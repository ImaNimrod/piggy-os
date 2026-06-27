#ifndef _VIP_MOTIONS_H
#define _VIP_MOTIONS_H

#include "editor.h"

enum motion {
    MOTION_LINE,
    MOTION_LINE_END,
    MOTION_WORDB,
    MOTION_WORDE,
    MOTION_WORDF,
};

void motion_left(struct editor_state* state);
void motion_right(struct editor_state* state);
void motion_up(struct editor_state* state);
void motion_down(struct editor_state* state);

void motion_buffer_bottom(struct editor_state* state);
void motion_buffer_top(struct editor_state* state);

void motion_find_backward(struct editor_state* state, char c);
void motion_find_forward(struct editor_state* state, char c);

void motion_line_start(struct editor_state* state);
void motion_line_end(struct editor_state* state);
void motion_line_last_nonwhitespace(struct editor_state* state);

void motion_page_down(struct editor_state* state, bool half);
void motion_page_up(struct editor_state* state, bool half);

void motion_word_backward(struct editor_state* state);
void motion_word_end(struct editor_state* state);
void motion_word_forward(struct editor_state* state);

bool motion_to_range(struct editor_state* state, enum motion motion, struct range* range);

#endif /* _VIP_MOTIONS_H */
