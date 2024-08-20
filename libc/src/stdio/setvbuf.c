#include <stdlib.h>
#include "stdio_internal.h"

int setvbuf(FILE* stream, char* buffer, int type, size_t size) {
    if (type == _IOFBF) {
        stream->flags |= FILE_FLAG_BUFFERED;
        stream->flags &= ~FILE_FLAG_LINEBUFFER;
    } else if (type == _IOLBF) {
        stream->flags |= FILE_FLAG_BUFFERED | FILE_FLAG_LINEBUFFER;
    } else if (type == _IONBF) {
        stream->flags &= ~(FILE_FLAG_BUFFERED | FILE_FLAG_LINEBUFFER);
    } else {
        return -1;
    }

    if (buffer && size >= UNGET_BYTES) {
        if (!(stream->flags & FILE_FLAG_USER_BUFFER)) {
            free(stream->buf);
        }

        stream->buf = (unsigned char*) buffer;
        stream->buf_size = size;
        stream->flags |= FILE_FLAG_USER_BUFFER;
    }

    return 0;
}
