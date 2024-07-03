#include <fcntl.h>
#include <stddef.h>
#include <sys/syscalls.h>
#include <sys/types.h> 
#include <unistd.h>

int chdir(const char* path) {
    int fd = open(path, O_PATH);
    if (fd < 0) {
        return -1;
    }
    return fchdir(fd);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

__attribute__((noreturn)) void _exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

int execve(const char* path, char* const* argv, char* const* envp) {
    return syscall3(SYS_EXEC, (uint64_t) path, (uint64_t) argv, (uint64_t) envp);
}

// TODO: implement execve variants

int fchdir(int fd) {
    return syscall1(SYS_CHDIR, fd);
}

pid_t fork(void) {
    return syscall0(SYS_FORK);
}

int ftruncate(int fd, off_t length) {
    return syscall2(SYS_TRUNCATE, fd, length);
}

char* getcwd(char* buf, size_t length) {
    return (char*) syscall2(SYS_GETCWD, (uint64_t) buf, length);
}

pid_t getpid(void) {
    return syscall0(SYS_GETPID);
}

off_t lseek(int fd, off_t offset, int whence) {
    return syscall3(SYS_SEEK, fd, offset, whence);
}

ssize_t read(int fd, void* buf, size_t count) {
    return syscall3(SYS_READ, fd, (uint64_t) buf, count);
}

int truncate(const char* path, off_t length) {
    int fd = open(path, O_WRONLY | (length == 0 ? O_TRUNC : 0));
    if (fd < 0) {
        return -1;
    }

    if (length == 0) {
        return 0;
    }
    return ftruncate(fd, length);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (uint64_t) buf, count);
}
