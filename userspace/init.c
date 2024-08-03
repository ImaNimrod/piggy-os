#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    if (getpid() != 1) {
        fputs("must be run from the init process (pid=1)", stderr);
        return EXIT_FAILURE;
    }

    char buffer[50];
    fputs(buffer, stderr);

    puts("starting PiggyOS...\n");

    setenv("HOME", "/home", 1);
    chdir(getenv("HOME"));
    setenv("PATH", "/bin", 1);

    pid_t pid = fork();
    if (pid < 0) {
        fputs("failed to fork child process", stderr);
        return EXIT_FAILURE;
    } else if (pid == 0) {
        char* const argv[] = { "/bin/sh", NULL };
        execv(argv[0], argv);
        return EXIT_FAILURE;
    } else {
        do {
            waitpid(-1, NULL, 0);
        } while (errno != ECHILD);
    }

    return EXIT_SUCCESS;
}
