#include <stdarg.h>
#include "stdio_internal.h"

int snprintf(char* str, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(str, size, fmt, args);
    va_end(args);
    return ret;
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list args) {
    return __vasnprintf(str, size, fmt, args);
}
