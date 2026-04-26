#include <sys/param.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "history.h"

#define HISTORY_MAX_LENGTH 100

static char* history[HISTORY_MAX_LENGTH];
static size_t history_end_index;
static size_t history_start_index;
static size_t history_total;

void history_print(size_t count) {
    count = MIN(count, history_end_index);

    for (size_t i = 0; i < count; i++) {
        size_t index = (history_end_index + HISTORY_MAX_LENGTH - count + i) % HISTORY_MAX_LENGTH;
        size_t absolute_index = history_total - (count - i);
        printf("%4zu: %s\n", absolute_index + 1, history[index]);
    }

}

void history_push(const char* line) {
    history[history_end_index] = realloc(history[history_end_index], strlen(line) + 1);
    if (history[history_end_index] == NULL) {
        err(EXIT_FAILURE, "realloc");
    }

    strcpy(history[history_end_index], line);

    history_total++;

    history_end_index = (history_end_index + 1) % HISTORY_MAX_LENGTH;
    if (history_end_index == history_start_index) {
        history_start_index = (history_start_index + 1) % HISTORY_MAX_LENGTH;
    }
}
