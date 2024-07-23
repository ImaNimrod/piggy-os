#include "stdio_internal.h"

void clearerr(FILE* stream) {
    stream->flags &= ~FILE_FLAG_ERROR;
}
