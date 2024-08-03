#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    for (char** ep = environ; *ep; ep++) {
        puts(*ep);
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
