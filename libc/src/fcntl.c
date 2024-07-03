#include <fcntl.h>
#include <sys/syscalls.h>

int creat(const char* path, ...) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

// TODO: implement fcntl
int fcntl(int fd, int cmd, ...) {
    (void) fd;
    (void) cmd;
    return -1;
}

int open(const char* path, int flags, ...) {
    return syscall2(SYS_OPEN, (uint64_t) path, flags);
}
