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

static void* memset64(void* dest, long c, size_t n) {
    __asm__ volatile("cld; rep stosq" : "=c"((int){0}) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
    return dest;
}

int main(void) {
    if (getpid() != 1) {
        fputs("init: must be run with process pid = 1\n", stderr);
    }

    int fb = open("/dev/fb0", O_WRONLY);
    if (fb < 0) {
        fputs("init: failed to open framebuffer device\n", stderr);
        for (;;) {
            __asm__ volatile("pause");
        }
    }

    struct stat fb_stat;
    if (fstat(fb, &fb_stat) < 0) {
        fputs("init: failed to stat framebuffer device\n", stderr);
        for (;;) {
            __asm__ volatile("pause");
        }
    }

    uint8_t* buf = sbrk(fb_stat.st_size);
    for (;;) {
        memset64(buf, 0xffff0000ffff0000 /* red */, fb_stat.st_size / sizeof(long));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset64(buf, 0xff00ff00ff00ff00 /* green */, fb_stat.st_size / sizeof(long));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset64(buf, 0xff0000ffff0000ff /* blue */, fb_stat.st_size / sizeof(long));
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
