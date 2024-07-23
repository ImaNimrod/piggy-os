#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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
    puts("starting framebuffer test loop...\n");

    int fb = open("/dev/fb0", O_WRONLY);
    if (fb < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    struct stat fb_stat;
    if (fstat(fb, &fb_stat) < 0) {
        perror("fstat");
        return EXIT_FAILURE;
    }

    char* buf = malloc(fb_stat.st_size);
    if (buf == NULL) {
        fputs("unable to allocate buffer for framebuffer image\n", stderr);
        return EXIT_FAILURE;
    }

    for (;;) {
        memset32(buf, 0xffff0000 /* red */, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset32(buf, 0xff00ff00 /* green */, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();

        memset32(buf, 0xff0000ff /* blue */, fb_stat.st_size / sizeof(int));
        lseek(fb, 0, SEEK_SET);
        write(fb, (void*) buf, fb_stat.st_size);

        delay();
    }

    free(buf);
    close(fb);

    return EXIT_SUCCESS;
}
