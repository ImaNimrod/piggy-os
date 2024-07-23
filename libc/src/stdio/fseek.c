#include <unistd.h>
#include "stdio_internal.h"

int fseek(FILE* stream, long offset, int whence) {
    if (stream->write_pos && fflush(stream) == EOF) {
        return -1;
    }

    if (whence == SEEK_CUR) {
        if (__builtin_sub_overflow(offset, stream->read_end - stream->read_pos, &offset)) {
            return -1;
        }
    }

    if (lseek(stream->fd, offset, whence) < 0) {
        return -1;
    }

    stream->flags &= ~FILE_FLAG_EOF;
    stream->read_pos = stream->read_end = UNGET_BYTES;
    return 0;
}
