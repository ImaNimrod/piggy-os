#include <stdarg.h>
#include "stdio_internal.h"

int vsnprintf(char* str, size_t size, const char* fmt, va_list args) {
    union callback_data cd = {
        .buf = {
            .str = str,
            .size = size,
            .written = 0,
        }
    };
    return __printf_internal(&cd, SNPRINTF, fmt, args);
}
