#include <sys/stat.h>
#include <sys/syscall.h>

int fstat(int fd, struct stat* stat) {
    return syscall2(SYS_STAT, fd, (uint64_t) stat);
}
