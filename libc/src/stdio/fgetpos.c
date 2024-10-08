#include "stdio_internal.h"

int fgetpos(FILE* stream, fpos_t* pos) {
    long ret = ftell(stream);
    if (ret < 0) {
        return -1;
    }

    *pos = ret;
    return 0;
}
