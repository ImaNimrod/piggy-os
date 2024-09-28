#include <fcntl.h>
#include <stdarg.h> 
#include <sys/syscall.h>

int fcntl(int fd, int cmd, ...) {
    int arg = 0;

    va_list args;
    va_start(args, cmd);
    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETFL:
            arg = va_arg(args, int);
    }
    va_end(args);

    return syscall3(SYS_FCNTL, fd, cmd, arg);
}
