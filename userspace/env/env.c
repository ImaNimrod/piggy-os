#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char** environ;

static void usage(void) {
    fprintf(stderr, "usage: env [-0i] [-C DIR] [-a ARG] [-u NAME] [NAME=VALUE]... [COMMAND [ARG]...]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    char* argv0 = NULL;
    char* cwd = NULL;
    char seperator = '\n';

    int c;
    while ((c = getopt(argc, argv, "0C:a:iu:")) != -1) {
        switch (c) {
            case '0':
                seperator = '\0';
                break;
            case 'C':
                cwd = optarg;
                break;
            case 'a':
                argv0 = optarg;
                break;
            case 'i':
                environ = (char**) { NULL };
                break;
            case 'u':
                if (unsetenv(optarg) < 0) {
                    err(EXIT_FAILURE, "unsetenv(%s)", optarg);
                }
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    size_t equals;
    int i = 0;
    while (i < argc && (equals = strcspn(argv[i], "=")) != strlen(argv[i])) {
        char* key = argv[i];
        key[equals] = '\0';
        char* value = argv[i] + equals + 1;

        if (setenv(key, value, 1) < 0) {
            err(EXIT_FAILURE, "setenv(%s=%s)", key, value);
        }

        i++;
    }

    if (argv[i] != NULL) {
        if (cwd != NULL) {
            if (chdir(cwd) < 0) {
                err(EXIT_FAILURE, "failed to change directory to '%s'", cwd);
            }
        }

        char* saved = argv[i];
        if (argv0 != NULL) {
            argv[i] = argv0;
        }

        execvp(saved, argv + i);
        err(EXIT_FAILURE, "execve");
    } else {
        if (argv0 != NULL) {
            warnx("must specify a command with -a");
            usage();
        }
    }

    for (char** env = environ; *env != NULL; env++) {
        printf("%s%c", *env, seperator);
    }

    return EXIT_SUCCESS;
}
