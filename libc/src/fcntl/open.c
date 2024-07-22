#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>

int open(const char* path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    int ret = syscall3(SYS_OPEN, (uint64_t) path, flags, va_arg(args, mode_t));
    va_end(args);
    return ret;
}
