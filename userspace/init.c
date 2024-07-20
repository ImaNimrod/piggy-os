#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

    uint32_t* buf = sbrk(fb_stat.st_size);
    memset(buf, 0xff, fb_stat.st_size / sizeof(uint32_t));
    write(fb, (void*) buf, fb_stat.st_size);

    close(fb);

    for (;;) {
        __asm__ volatile("pause");
    }
    return EXIT_SUCCESS;
}
