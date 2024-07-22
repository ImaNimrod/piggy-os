#include <sys/syscall.h>
#include <unistd.h>

int ftruncate(int fd, off_t length) {
    return syscall2(SYS_TRUNCATE, fd, length);
}
