#include <stdarg.h>
#include "stdio_internal.h"

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
	return ret;
}
