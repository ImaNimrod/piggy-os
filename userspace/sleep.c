#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "sleep"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " NUMBER[SUFFIX]...\n   or: " PROGRAM_NAME " OPTION\n\nPause for NUMBER seconds. SUFFIX may be 's' for seconds (the default), 'm' for minutes, 'h' for hours, or 'd' for days.\nNUMBER must be an integer. Given two or more arguments, pause for the amount of time specified by the sum of their values.\n\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

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
    int c;
    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
            case 'h': 
                help();
                break;
            case '?':
                error();
                break;
        }
    }

    if (optind >= argc) {
        fputs(PROGRAM_NAME ": missing operand\n", stderr);
        error();
    }

    unsigned long int seconds = 0;
    for (int i = optind; i < argc; i++) {
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
            error();
        }

        seconds += s;
    }

    if (sleep(seconds) < 0) {
        perror(PROGRAM_NAME);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
