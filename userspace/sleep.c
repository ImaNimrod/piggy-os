#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "sleep"

static bool apply_suffix(long int* seconds, char suffix) {
    int multiplier;

    switch (suffix) {
        case '\0':
        case 's':
            multiplier = 1;
            break;
        case 'm':
            multiplier = 60;
            break;
        case 'h':
            multiplier = 3600;
            break;
        case 'd':
            multiplier = 86400;
            break;
        default:
            return false;
    }

    *seconds *= multiplier;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        return EXIT_FAILURE;
    }

    unsigned long int seconds = 0;
    for (int i = 1; i < argc; i++) {
        size_t len = strlen(argv[i]) - 1;

        char suffix = argv[i][len];
        if (!isalpha(suffix)) {
            suffix = '\0';
        } else {
            argv[i][len] = '\0';
        }

        long s = strtol(argv[i], NULL, 10);
        if (errno != 0 || s < 0 || !apply_suffix(&s, suffix)) {
            fprintf(stderr, PROGRAM_NAME ": invalid time interval '%s'\n", argv[1]);
            return EXIT_FAILURE;
        }

        seconds += s;
    }

    if (sleep(seconds) < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
