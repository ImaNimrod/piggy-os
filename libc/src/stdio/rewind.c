#include "stdio_internal.h"

void rewind(FILE* stream) {
    fseek(stream, 0, SEEK_SET);
}
