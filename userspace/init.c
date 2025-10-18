#include <stdio.h>
#include <unistd.h>

int main() {
    while (1) {
        printf("hello, world!\n");
        usleep(500000);
    }

    return 0;
}
