#include <stdarg.h>
#include <stdint.h>
#include "stdio_internal.h"

int sprintf(char* str, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsprintf(str, fmt, args);
    va_end(args);
    return ret;
}

int vsprintf(char* str, const char* fmt, va_list args) {
    return __vasnprintf(str, SIZE_MAX, fmt, args);
}
