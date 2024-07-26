#include <stdarg.h>
#include "stdio_internal.h" 

int fscanf(FILE* stream, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vfscanf(stream, fmt, args);
    va_end(args);
    return ret;
}

int vfscanf(FILE* stream, const char* fmt, va_list args) {
	return 0;
}
