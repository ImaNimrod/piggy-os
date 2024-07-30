#include <stdarg.h>
#include "stdio_internal.h"

int vfprintf(FILE* stream, const char* fmt, va_list args) {
    union callback_data cb = { .stream = stream };
    return __printf_internal(&cb, FPRINTF, fmt, args);
}
