#include "stdio_internal.h"

int ungetc(int c, FILE* stream) {
    if (c == EOF) {
        return EOF;
    }

    if (stream->write_pos != 0 && fflush(stream) == EOF) {
        return EOF;
    }

    if (stream->read_pos == 0) {
        return EOF;
    }

    stream->buf[stream->read_pos--] = (unsigned char) c;
    stream->flags &= ~FILE_FLAG_EOF;

    return (unsigned char) c;
}
