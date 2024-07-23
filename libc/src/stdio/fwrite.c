#include <string.h>
#include "stdio_internal.h"

size_t fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
    size_t bytes = size * count;
    if (!bytes) {
        return 0;
    }

    const unsigned char* p = (const unsigned char*) ptr;

    if (!(stream->flags & FILE_FLAG_BUFFERED)) {
        return __write_bytes(stream, p, bytes) / size;
    }

    if (bytes > BUFSIZ - stream->write_pos) {
        if (__write_bytes(stream, stream->buf, stream->write_pos) < stream->write_pos) {
            stream->write_pos = 0;
            return 0;
        }
        stream->write_pos = 0;
    }

    if (bytes > BUFSIZ) {
        return __write_bytes(stream, p, bytes) / size;
    }

    size_t written = 0;
    if (stream->flags & FILE_FLAG_TTY) {
        size_t i = bytes;
        while (i > 0 && p[i - 1] != '\n') {
            i--;
        }

        if (i > 0) {
            memcpy(stream->buf + stream->write_pos, p, i);

            written = __write_bytes(stream, stream->buf, stream->write_pos + i);
            if (written < stream->write_pos + i) {
                written = written <= stream->write_pos ? 0 : written - stream->write_pos;
                stream->write_pos = 0;
                return written / size;
            }

            written = i;
            stream->write_pos = 0;
        }
    }

    memcpy(stream->buf + stream->write_pos, p + written, bytes - written);
    stream->write_pos += bytes - written;
    return count;
}
