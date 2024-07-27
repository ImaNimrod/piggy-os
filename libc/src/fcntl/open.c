#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>

int open(const char* path, int flags, ...) {
    return syscall2(SYS_OPEN, (uint64_t) path, flags);
}
