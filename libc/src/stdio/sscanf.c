#include <stdarg.h>
#include "stdio_internal.h"

int sscanf(const char* str, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsscanf(str, fmt, args);
    va_end(args);
    return ret;
}

int vsscanf(const char* str, const char* fmt, va_list args) {
    return 0;
}
