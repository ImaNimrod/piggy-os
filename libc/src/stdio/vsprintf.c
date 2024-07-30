#include <stdarg.h>
#include "stdio_internal.h"

int vsprintf(char* str, const char* fmt, va_list args) {
    union callback_data cd = {
        .buf = {
            .str = str,
            .size = 0,
            .written = 0,
        }
    };
    return __printf_internal(&cd, SPRINTF, fmt, args);
}
