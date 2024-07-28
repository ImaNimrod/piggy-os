#include <unistd.h>
#include "stdio_internal.h"

int fflush(FILE* stream) {
    if (!stream) {
        int ret = 0;
        ret |= fflush(stdin);
        ret |= fflush(stdout);
        ret |= fflush(stderr);

        FILE* iter = __file_list_head;
        while (iter) {
            ret |= fflush(iter);
            iter = iter->next;
        }

        return ret;
    }

    if (stream->read_pos != 0) {
        off_t offset = -(off_t) (stream->read_end - stream->read_pos);
        if (stream->read_pos < UNGET_BYTES) {
            stream->read_pos = UNGET_BYTES;
        }

        if (lseek(stream->fd, offset, SEEK_CUR) < 0) {
            stream->flags |= FILE_FLAG_ERROR;
            return EOF;
        }

        stream->read_pos = stream->read_end = UNGET_BYTES;
    }

    if (stream->write_pos != 0) {
        if (__write_bytes(stream, stream->buf, stream->write_pos) < stream->write_pos) {
            stream->write_pos = 0;
            return EOF;
        }

        stream->write_pos = 0;
    }

    return 0;
}
