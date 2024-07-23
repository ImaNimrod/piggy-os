#include "stdio_internal.h"

int ferror(FILE* stream) {
    return stream->flags & FILE_FLAG_ERROR;
}
