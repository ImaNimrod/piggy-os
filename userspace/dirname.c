#include <libgen.h>
#include <stdio.h> 
#include <stdlib.h> 

#define PROGRAM_NAME "dirname"

int main(int argc, char** argv) {
    if (argc < 2) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        return EXIT_FAILURE;
    }

    char* dir;
    for (int i = 1; i < argc; i++) {
        dir = dirname(argv[i]);
        puts(dir);
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
