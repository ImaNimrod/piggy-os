#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char* argv[]) {
    for (int i = 1; i < argc; i++) {
        puts(argv[i]);
        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    putchar('\n');
    return EXIT_SUCCESS;
}
