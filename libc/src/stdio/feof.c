#include "stdio_internal.h"

int feof(FILE* stream) {
    return stream->flags & FILE_FLAG_EOF;
}
