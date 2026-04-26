#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: chroot NEWROOT [COMMAND [ARG]...]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    const char* new_root = argv[0];
    if (chroot(new_root) < 0) {
        err(EXIT_FAILURE, "cannot change root to '%s'", new_root);
    }
    if (chdir("/") < 0) {
        err(EXIT_FAILURE, "failed to chdir to new root directory");
    }

    if (argv[1] != NULL) {
        execvp(argv[1], &argv[1]);
        err(EXIT_FAILURE, "%s", argv[1]);
    }

    const char* shell;
    if ((shell = getenv("SHELL")) == NULL) {
        shell = "/usr/bin/sh";
    }

    execlp(shell, shell, "-i", NULL);
    err(EXIT_FAILURE, "execlp");
}
