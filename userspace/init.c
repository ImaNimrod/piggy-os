#include <fcntl.h> 
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

static inline void hang(void) {
    for (;;) {
        __asm__ volatile("pause");
    }
}

int main(void) {
    int fb = open("/dev/fb0", O_WRONLY);
    if (fb < 0) {
        hang();
    }

    struct stat fb_stat;
    if (fstat(fb, &fb_stat) < 0) {
        hang();
    }

    char* buf = malloc(fb_stat.st_size);
    if (buf == NULL) {
        hang();
    }

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

    free(buf);
    close(fb);

    hang();
    return EXIT_SUCCESS;
}
