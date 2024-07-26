#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void delay(void) {
    size_t i = 100000000;
    do {} while (i--);
}

int main(void) {
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

    int rand = open("/dev/random", O_RDONLY);
    if (rand < 0) {
        perror("rand");
        return EXIT_FAILURE;
    }

    char* buf = malloc(fb_stat.st_size);
    if (buf == NULL) {
        fputs("unable to allocate buffer for framebuffer image\n", stderr);
        return EXIT_FAILURE;
    }

    for (;;) {
        read(rand, buf, fb_stat.st_size);
        write(fb, buf, fb_stat.st_size);
        lseek(fb, 0, SEEK_SET);

        delay();
    }

    free(buf);
    close(fb);

    return EXIT_SUCCESS;
}
