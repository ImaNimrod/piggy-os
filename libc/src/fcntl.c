#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>

int creat(const char* path, mode_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

// TODO: implement fcntl
int fcntl(int fd, int cmd, ...) {
    (void) fd;
    (void) cmd;
    return -1;
}

int open(const char* path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    int ret = syscall3(SYS_OPEN, (uint64_t) path, flags, va_arg(args, mode_t));
    va_end(args);
    return ret;
}
