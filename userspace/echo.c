#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
    bool do_trailing_newline = true;

    int c;
    while ((c = getopt(argc, argv, "n"))) {
        switch (c) {
            case 'n':
                do_trailing_newline = false;
                break;
        }
    }

    for (int i = optind; i < argc; i++) {
        puts(argv[i]);
        if (i + 1 < argc) {
            putchar(' ');
        }
    }

    if (do_trailing_newline) {
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
