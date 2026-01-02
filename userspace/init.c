#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h> 
#include <piggy/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void mount_filesystems(void) {
    if (mkdir(_PATH_DEV, 0) < 0 && errno != EEXIST) {
        errx(EXIT_FAILURE, "failed to mkdir %s for devfs", _PATH_DEV);
    }
    if (mount(NULL, _PATH_DEV, "devfs") < 0) {
        errx(EXIT_FAILURE, "failed to mount devfs");
    }

    if (mkdir(_PATH_TMP, 0) < 0 && errno != EEXIST) {
        errx(EXIT_FAILURE, "failed to mkdir %s for tmpfs", _PATH_TMP);
    }
    if (mount(NULL, _PATH_TMP, "tmpfs") < 0) {
        errx(EXIT_FAILURE, "failed to mount tmpfs");
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

int main() {
    if (getpid() != 1) {
        errx(EXIT_FAILURE, "must be run from pid = 1");
    }

    mount_filesystems();

    puts("\nhey pig...\n");
    fflush(stdout);

    for (;;) {
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

            char* envp[] = {
                "HOME=/home",
                "PATH=/usr/bin",
                "TZ=UTC0",
                NULL,
            };

            execve(argv[0], argv, envp);
            err(EXIT_FAILURE, "execve");
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }

    // should be impossible
    return EXIT_FAILURE;
}
