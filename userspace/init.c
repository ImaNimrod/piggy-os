#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    if (getpid() != 1) {
        fputs("must be run from the init process (pid=1)\n", stderr);
        return EXIT_FAILURE;
    }

    puts("starting PiggyOS...\n");

    setenv("HOME", "/home", 1);
    if (chdir(getenv("HOME")) < 0) {
        perror("chdir");
        return EXIT_FAILURE;
    }

    setenv("PATH", "/bin", 1);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    } else if (pid == 0) {
        char* const argv[] = { "/bin/sh", NULL };

        if (execv(argv[0], argv) < 0) {
            perror("execv");
        }
        return EXIT_FAILURE;
    }

    do {
        wait(NULL);
    } while (errno != ECHILD);

    return EXIT_SUCCESS;
}
