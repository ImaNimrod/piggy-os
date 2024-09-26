#include <sys/syscall.h>
#include <unistd.h>

int fsync(int fd) {
    return syscall1(SYS_FSYNC, fd);
}
