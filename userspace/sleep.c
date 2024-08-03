#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PROGRAM_NAME "sleep"

int main(int argc, char** argv) {
    if (argc == 1) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        return EXIT_FAILURE;
    }

    unsigned int seconds = (unsigned int) atol(argv[1]);
    /*
       fprintf(stderr, PROGRAM_NAME ": invalid time interval '%s'\n", argv[1]);
       return EXIT_FAILURE;
       */

    if (sleep(seconds) < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
