#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        puts("hello from the child process\n");
    } else if (pid < 0) {
        puts("failed to fork!\n");
    } else {
        puts("hello from the parent process\n");
    }

    while (1) {}
    return 0;
}
