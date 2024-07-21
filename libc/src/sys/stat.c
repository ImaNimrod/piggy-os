#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int fstat(int fd, struct stat* stat) {
    return syscall2(SYS_STAT, fd, (uint64_t) stat);
}

int stat(const char* path, struct stat* stat) {
    int fd = open(path, O_PATH);
    if (fd < 0) {
        return -1;
    }

    int ret = fstat(fd, stat);
    close(fd);
    return ret;
}
