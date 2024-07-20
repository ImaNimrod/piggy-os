#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>


int main(void) {
    if (getpid() != 1) {
        fputs("init: must be run with process pid = 1\n", stderr);
    }


    for (;;) {
        puts("\ninit: starting PiggyOS\n");

        pid_t pid = fork();

        if (pid == -1) {
            fputs("init: fork failed\n", stderr);
        }

        if (pid == 0) {
            char* argv[] = { "/bin/sh", NULL };
            char* envp[] = { NULL };
            execve(argv[0], argv, envp);
            return EXIT_FAILURE;
        } else {
            waitpid(pid, NULL, 0);
        }
    }

    return EXIT_SUCCESS;
}
