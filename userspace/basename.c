#include <libgen.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h>

#define PROGRAM_NAME "basename"

int main(int argc, char** argv) {
    if (argc < 2) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        return EXIT_FAILURE;
    } else if (argc > 3) {
        fprintf(stderr, PROGRAM_NAME ": extra operand '%s'\n", argv[3]);
        return EXIT_FAILURE;
    }

    char* base = basename(argv[1]);
    size_t base_len = strlen(base);

    if (argc > 2) {
        const char* suffix = argv[2];
        size_t suffix_len = strlen(suffix);
        if (base_len > suffix_len) {
            size_t length = base_len - suffix_len;
            if (!strcmp(base + length, suffix)) {
                base_len = length;
            }
        }
    }

    base[base_len] = '\0';
    puts(base);
    putchar('\n');

    return EXIT_SUCCESS;
}
