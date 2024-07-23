#include "stdio_internal.h"

int fileno(FILE* stream) {
    return stream->fd;
}
