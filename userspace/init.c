#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();

    if (pid == 0) {
        puts("hello from the child process\n");
        return EXIT_SUCCESS;
    } else if (pid < 0) {
        puts("failed to fork!\n");
    } else {
        int status;
        wait(&status);
        puts("hello from the parent process\n");
    }

    while (1) {}
    return EXIT_SUCCESS;
}
