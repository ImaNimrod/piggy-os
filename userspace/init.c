#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    if (getpid() != 1) {
        errx(EXIT_FAILURE, "must be run from pid = 1");
    }

    for (;;) {
        pid_t pid = fork();
        if (pid < 0) {
            err(EXIT_FAILURE, "fork failed");
        } else if (pid == 0) {
            chdir("/home");

            char* argv[] = {
                "/usr/bin/sh",
                NULL,
            };

            char* envp[] = {
                "HOME=/home",
                "PATH=/usr/bin",
                NULL,
            };

            if (execve(argv[0], argv, envp) < 0) {
                err(EXIT_FAILURE, "execve failed");
            }

            _exit(EXIT_FAILURE);
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }

    // should be impossible
    return EXIT_FAILURE;
}
