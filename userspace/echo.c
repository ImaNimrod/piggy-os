#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    (void) argc;

    bool trailing_newline = true;

    if (*argv++ != NULL && strcmp(*argv, "-n") == 0) {
        trailing_newline = false;
        argv++;
    }

    while (*argv != NULL) {
        fputs(*argv, stdout);

        argv++;
        if (*argv != NULL) {
            putchar(' ');
        }
    }

    if (trailing_newline) {
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
