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
#define SYS_OPEN            9
#define SYS_CLOSE           10
#define SYS_READ            11
#define SYS_WRITE           12
#define SYS_IOCTL           13
#define SYS_SEEK            14
#define SYS_CHDIR           15
#define SYS_GETCWD          16

extern uint64_t syscall0(uint64_t syscall_number);
extern uint64_t syscall1(uint64_t syscall_number, uint64_t arg1);
extern uint64_t syscall2(uint64_t syscall_number, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3);

static uint32_t buf[800 * 600];

static void hang(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

void* memset32(void* dest, uint32_t c, size_t n) {
    uint32_t* buf = dest;

    while (n--) {
        *buf++ = c;
    }

    return dest;
}

int open(const char* path, int flags) {
    return syscall2(SYS_OPEN, (uint64_t) path, flags);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

long write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (uint64_t) buf, count);
}

long seek(int fd, long offset, int whence) {
    return syscall3(SYS_SEEK, fd, offset, whence);
}

void puts(const char* str) {
    syscall1(SYS_DEBUG, (uint64_t) str);
}

int main(void) {
    int fb = open("/dev/fb0", O_WRONLY);
    if (fb < 0) {
        puts("failed to open framebuffer device");
        hang();
    }

    long size = seek(fb, 0, SEEK_END);
    seek(fb, 0, SEEK_SET);

    while (1) {
        memset32(buf, 0xffff0000, sizeof(buf) / 4);
        write(fb, buf, sizeof(buf));
        seek(fb, 0, SEEK_SET);

        for (int i = 0; i < 100000000; i++) {
            __asm__ volatile("nop");
        }

        memset32(buf, 0xff0000ff, sizeof(buf) / 4);
        write(fb, buf, sizeof(buf));
        seek(fb, 0, SEEK_SET);

        for (int i = 0; i < 100000000; i++) {
            __asm__ volatile("nop");
        }

        memset32(buf, 0xff00ff00, sizeof(buf) / 4);
        write(fb, buf, sizeof(buf));
        seek(fb, 0, SEEK_SET);

        for (int i = 0; i < 100000000; i++) {
            __asm__ volatile("nop");
        }
    }

    close(fb);

    hang();
    return 0;
}
