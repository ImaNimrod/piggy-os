#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        puts(argv[i]);
        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    putchar('\n');
    return EXIT_SUCCESS;
}
