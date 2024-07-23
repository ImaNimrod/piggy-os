#include <stdlib.h>
#include "stdio_internal.h"

FILE* fdopen(int fd, const char* mode) {
    (void) mode;

    FILE* stream = malloc(sizeof(FILE));
    if (stream == NULL) {
        return NULL;
    }

    stream->fd = fd;
    stream->flags = FILE_FLAG_BUFFERED;

    stream->buf = malloc(BUFSIZ);
    if (!stream->buf) {
        free(stream);
        return NULL;
    }
    stream->buf_size = BUFSIZ;

    stream->read_pos = stream->read_end = UNGET_BYTES;
    stream->write_pos = 0;

    stream->prev = NULL;
    stream->next = __file_list_head;
    if (stream->next) {
        stream->next->prev = stream;
    }
    __file_list_head = stream;

    return stream;
}
