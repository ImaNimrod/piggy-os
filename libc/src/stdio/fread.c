#include <string.h>
#include "stdio_internal.h"

size_t fread(void* ptr, size_t size, size_t count, FILE* stream) {
    size_t bytes = size * count;
    if (!bytes) {
        return 0;
    }

    if (stream->flags & FILE_FLAG_EOF) {
        return 0;
    }

    unsigned char* p = (unsigned char*) ptr;

    size_t buf_filled = stream->read_end - stream->read_pos;
    if (buf_filled <= bytes) {
        memcpy(p, stream->buf + stream->read_pos, buf_filled);
        stream->read_pos = UNGET_BYTES;
        stream->read_end = UNGET_BYTES;
    } else {
        memcpy(p, stream->buf + stream->read_pos, bytes);
        stream->read_pos += bytes;
        return count;
    }

    size_t remaining = bytes - buf_filled;
    if (remaining == 0) {
        return count;
    }

    if (remaining >= BUFSIZ - stream->read_end || !(stream->flags & FILE_FLAG_BUFFERED)) {
        size_t read = __read_bytes(stream, p + buf_filled, remaining);
        return (buf_filled + read) / size;
    }

    size_t read = __read_bytes(stream, stream->buf + stream->read_end, BUFSIZ - stream->read_end);
    stream->read_end += read;

    size_t copied = remaining > read ? read : remaining;
    memcpy(p + buf_filled, stream->buf + stream->read_pos, copied);
    stream->read_pos += copied;

    return (buf_filled + copied) / size;
}
