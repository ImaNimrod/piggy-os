#include <sys/syscall.h>
#include <unistd.h>

ssize_t read(int fd, void* buf, size_t count) {
    return syscall3(SYS_READ, fd, (uint64_t) buf, count);
}
