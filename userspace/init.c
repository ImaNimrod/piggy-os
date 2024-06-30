#include <stddef.h>
#include <stdint.h>

#define O_RDONLY    0000
#define O_WRONLY    0001
#define O_RDWR      0002

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

#define SYS_DEBUG           0
#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_EXEC            3
#define SYS_YIELD           4
#define SYS_GETPID          5
#define SYS_GETTID          6
#define SYS_THREAD_CREATE   7
#define SYS_THREAD_EXIT     8
#define SYS_SBRK            9
#define SYS_OPEN            10
#define SYS_CLOSE           11
#define SYS_READ            12
#define SYS_WRITE           13
#define SYS_IOCTL           14
#define SYS_SEEK            15
#define SYS_CHDIR           16
#define SYS_GETCWD          17

extern uint64_t syscall0(uint64_t syscall_number);
extern uint64_t syscall1(uint64_t syscall_number, uint64_t arg1);
extern uint64_t syscall2(uint64_t syscall_number, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3);

void exit(int status) {
    syscall1(SYS_EXIT, status);
}

int fork(void) {
    return syscall0(SYS_FORK);
}

void* sbrk(intptr_t size) {
    return (void*) syscall1(SYS_SBRK, size);
}

void hang(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

void puts(const char* str) {
    syscall1(SYS_DEBUG, (uint64_t) str);
}

int open(const char* path, int flags) {
    return syscall2(SYS_OPEN, (uint64_t) path, flags);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

long read(int fd, void* buf, size_t count) {
    return syscall3(SYS_READ, fd, (uint64_t) buf, count);
}

long write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (uint64_t) buf, count);
}

long seek(int fd, long offset, int whence) {
    return syscall3(SYS_SEEK, fd, offset, whence);
}

int main(void) {
    puts("hello, world!\n");

    hang();
    return 0;
}
