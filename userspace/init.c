#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void delay(void) {
    size_t i = 100000000;
    do {} while (i--);
}

static void* memset32(void* dest, int c, size_t n) {
    __asm__ volatile("cld; rep stosl" : "=c"((int){0}) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
    return dest;
}

int main(void) {
    if (getpid() != 1) {
        fputs("init: must be run with process pid = 1\n", stderr);
    }

    int fb = open("/dev/fb0", O_WRONLY);
    if (fb < 0) {
        for (;;) {
            __asm__ volatile("pause");
        }
    }

    struct stat fb_stat;
    if (fstat(fb, &fb_stat) < 0) {
        for (;;) {
            __asm__ volatile("pause");
        }
    }

    uint8_t* buf = sbrk(fb_stat.st_size);
    for (;;) {
        memset32(buf, 0xffff0000, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset32(buf, 0xff00ff00, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset32(buf, 0xff0000ff, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();
    }

    close(fb);

    for (;;) {
        __asm__ volatile("pause");
    }
    return EXIT_SUCCESS;
}
