#include <stdio.h>
#include <unistd.h>

int main() {
    //execl("/bin/fbdoom", "-iwad", "doom.wad", NULL);

    for (;;) {
        puts("hello, world!");
        sleep(1);
    }

    return 0;
}
