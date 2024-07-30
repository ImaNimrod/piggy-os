#include <stdarg.h>
#include "stdio_internal.h"

int vprintf(const char* fmt, va_list args) {
    union callback_data cb = { .stream = stdout };
    return __printf_internal(&cb, FPRINTF, fmt, args);
}
