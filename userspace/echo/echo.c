#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    bool trailing_newline = true;

    int i = 1;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        trailing_newline = false;
        i++;
    }

    for (; i < argc; i++) {
        fputs(argv[i], stdout);

        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    if (trailing_newline) {
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
