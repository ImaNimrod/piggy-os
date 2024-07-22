#include <sys/syscall.h>
#include <unistd.h>

ssize_t write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (uint64_t) buf, count);
}
