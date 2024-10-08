#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "env"

static void error(void) {
    fputs("try '" PROGRAM_NAME " -h' for more information\n", stderr);
    exit(EXIT_FAILURE);
}

static void help(void) {
    puts("usage: " PROGRAM_NAME " [OPTION]... [NAME=VALUE]... [COMMAND [ARG]...]\n\nSet each NAME to VALUE in the environment and run COMMAND. If no COMMAND is specified, print the current environment.\n\n-i\tstart with an empty environment\n-0\tend each output line with '\\0', not newline '\\n'\n-C DIR    change working directory to DIR\n-h\tdisplay this help and exit\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    char* empty_env[] = { NULL };
    char seperator = '\n';
    char* cwd = NULL;

    int c;
    while ((c = getopt(argc, argv, "hi0C:")) != -1) {
        switch (c) {
            case 'i':
                environ = empty_env;
                break;
            case '0':
                seperator = '\0';
                break;
            case 'C':
                cwd = optarg;
                break;
            case 'h':
                help();
                break;
            case ':':
            case '?':
                error();
                break;
        }
    }

    size_t equals;
    int i = optind;
    while (i < argc && (equals = strcspn(argv[i], "=")) != strlen(argv[i])) {
        char* key = argv[i];
        key[equals] = '\0';
        char* val = argv[i] + equals + 1;
        setenv(key, val, 1);
        i++;
    }

    if (argv[i]) {
        if (cwd) {
            if (chdir(cwd) < 0) {
                fprintf(stderr, PROGRAM_NAME ": unable to change working directory to '%s': %s\n", cwd, strerror(errno));
                return EXIT_FAILURE;
            }
        }

        execvp(argv[i], argv + i);
        return EXIT_FAILURE;
    }

    for (char** ep = environ; *ep; ep++) {
        puts(*ep);
        putchar(seperator);
    }

    return EXIT_SUCCESS;
}
