#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    bool print_args = false;
    if (argc >= 2) {
        print_args = true;
    }

    while (1) {
        if (print_args) {
            for (char** arg = argv + 1; *arg; arg++) {
                puts(*arg);
                putchar(' ');
            }
        } else {
            putchar('y');
        }

        putchar('\n');
    }

    return EXIT_SUCCESS;
}
