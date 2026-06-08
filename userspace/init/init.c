#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void run_rc_script(void) {
    pid_t pid = fork();
    if (pid < 0) {
        err(EXIT_FAILURE, "fork");
    }

    if (pid == 0) {
        char* const argv[] = { "/usr/bin/sh", "/etc/rc", NULL };
        execve(argv[0], argv, (char* const []) { NULL });
        err(EXIT_FAILURE, "execve");
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WEXITSTATUS(status)) {
        err(EXIT_FAILURE, "/etc/rc failed with status %d\n", WEXITSTATUS(status));
    }
}

static int switch_terminal(const char* tty) {
    int rfd = open(tty, O_RDONLY);
    if (rfd < 0) {
        return -1;
    }
    int wfd = open(tty, O_WRONLY);
    if (wfd < 0) {
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if (dup2(rfd, STDIN_FILENO) < 0) {
        return -1;
    }
    if (dup2(wfd, STDOUT_FILENO) < 0) {
        return -1;
    }
    if (dup2(wfd, STDERR_FILENO) < 0) {
        return -1;
    }

    close(rfd);
    close(wfd);

    return 0;
}

int main(void) {
    if (getpid() != 1) {
        errx(EXIT_FAILURE, "init must be run from PID = 1");
    }

    run_rc_script();

    puts("\nhey pig...\n");
    fflush(stdout);

    setenv("HOME", "/home", 1);
    setenv("PATH", _PATH_DEFPATH, 1);
    setenv("TERM", "linux", 1);

    pid_t pid = fork();
    if (pid < 0) {
        err(EXIT_FAILURE, "fork failed");
    } else if (pid == 0) {
        if (switch_terminal("/dev/tty") < 0) {
            err(EXIT_FAILURE, "failed to setup tty for shell");
        }

        chdir("/home");

        char* argv[] = {
            "/usr/bin/sh",
            NULL,
        };

        execv(argv[0], argv);
        err(EXIT_FAILURE, "execve");
    }

    for (;;) {
        pid_t pid;
        int status;

        while ((pid = waitpid(-1, &status, 0)) >= 0) {
            continue;
        }

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno = ECHILD) {
                pause();
            }
        }
    }

    __builtin_unreachable();
}
