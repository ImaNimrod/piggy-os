#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/syscalls.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    int ret = syscall3(SYS_IOCTL, fd, request, (uint64_t) va_arg(args, void*));
    va_end(args);
    return ret;
}
