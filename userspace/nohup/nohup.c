#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: nohup COMMAND [ARG]...\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        errx(EXIT_FAILURE, "missing operand");
    }

    if (isatty(STDIN_FILENO)) {
        close(STDIN_FILENO);

        int fd = open(_PATH_DEVNULL, O_RDONLY);
        if (fd < 0) {
            err(EXIT_FAILURE, "%s", _PATH_DEVNULL);
        }
    }

    if (isatty(STDOUT_FILENO)) {
        int fd = open("nohup.out", O_WRONLY | O_CREAT | O_APPEND);
        if (fd < 0) {
            err(EXIT_FAILURE, "nohup.out");
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            err(EXIT_FAILURE, "dup2");
        }

        close(fd);
    }

    if (isatty(STDERR_FILENO)) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            err(EXIT_FAILURE, "dup2");
        }
    }

    if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
        err(EXIT_FAILURE, "signal(SIGHUP)");
    }

    execvp(argv[0], argv);
    err(EXIT_FAILURE, "execvp");
}
