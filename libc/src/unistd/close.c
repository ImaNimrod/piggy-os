#include <sys/syscall.h>
#include <unistd.h>

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}
