#include <stdlib.h>
#include <unistd.h>
#include "stdio_internal.h"

int fclose(FILE* stream) {
    int ret = 0;

    if (fflush(stream) == EOF) {
        ret = EOF;
    } 

    if (stream == __file_list_head) {
        __file_list_head = stream->next;
    }
    if (stream->prev) {
        stream->prev->next = stream->next;
    }
    if (stream->next) {
        stream->next->prev = stream->prev;
    }

    free(stream->buf);

    if (stream->fd != -1 && close(stream->fd) < 0) {
        ret = EOF;
    }

    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }

    return ret;
}
