#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: pwd [-LP]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool physical = false;

    int c;
    while ((c = getopt(argc, argv, "LP")) != -1) {
        switch (c) {
            case 'L':
                physical = false;
                break;
            case 'P':
                physical = true;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 0) {
        warnx("extra operands provided");
        usage();
    }

    if (!physical) {
        char* pwd_env = getenv("PWD");
        if (pwd_env != NULL && pwd_env[0] == '/') {
            puts(pwd_env);
            return EXIT_SUCCESS;
        }
    }

    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        err(EXIT_FAILURE, "getcwd");
    }

    puts(buf);
    return EXIT_SUCCESS;
}
