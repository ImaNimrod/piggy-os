#include <fcntl.h>
#include <unistd.h>
#include "stdio_internal.h"

long ftell(FILE* stream) {
    long offset = lseek(stream->fd, 0, SEEK_CUR);
    if (offset < 0) {
        return offset;
    }

    offset += stream->write_pos;
    offset -= stream->read_end - stream->read_pos;

    return offset;
}
