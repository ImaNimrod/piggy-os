#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"

static char* history[HISTORY_MAX_LENGTH];
static ssize_t history_cursor = -1;
static size_t history_total;

static ssize_t history_start_index;
static ssize_t history_end_index;

static inline size_t relative_count(void) {
    return MIN(history_total, HISTORY_MAX_LENGTH);
}

static inline size_t relative_to_absolute(size_t relative) {
    return history_total - relative_count() + relative;
}

static const char* get_relative(size_t relative) {
    if (relative >= relative_count()) {
        return NULL;
    }

    return history[(history_start_index + relative) % HISTORY_MAX_LENGTH];
}

const char* history_next(void) {
    if (history_cursor == -1) {
        return NULL;
    }

    size_t count = relative_count();
    if ((size_t) history_cursor == count - 1) {
        history_cursor = -1;
        return "";
    }

    history_cursor++;
    return get_relative(history_cursor);
}

const char* history_prev(void) {
    size_t count = relative_count();
    if (count == 0) {
        return NULL;
    }

    if (history_cursor == -1) {
        history_cursor = count - 1;
    } else if (history_cursor > 0) {
        history_cursor--;
    }

    return get_relative(history_cursor);
}

void history_print(size_t count) {
    count = MIN(count, relative_count());

    size_t first = relative_count() - count;

    for (size_t i = first; i < relative_count(); i++) {
        printf("%4zu: %s\n", relative_to_absolute(i) + 1, get_relative(i));
    }
}

void history_push(const char* line) {
    size_t len = strlen(line);
    char* slot = history[history_end_index];

    char* new = realloc(slot, len + 1);
    if (!new) {
        errx(EXIT_FAILURE, "realloc");
    }

    history[history_end_index] = new;
    memcpy(history[history_end_index], line, len + 1);

    history_total++;

    history_end_index = (history_end_index + 1) % HISTORY_MAX_LENGTH;

    if (history_end_index == history_start_index) {
        history_start_index = (history_start_index + 1) % HISTORY_MAX_LENGTH;
    }

    history_cursor = -1;
}
