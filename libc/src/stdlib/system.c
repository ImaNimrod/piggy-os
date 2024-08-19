#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "stdlib_internal.h"

int system(const char* command) {
    if (!command) {
        int fd = open("/bin/sh", O_PATH);
        int ret;
        if (!(ret = (fd < 0))) {
            close(fd);
        }
        return ret;
    }

    int status;

    pid_t pid = fork();
    if (pid < 0) {
        status = -1;
    } else if (pid == 0) {
        execl("/bin/sh", "sh", "-c", "--", command, NULL);
        _exit(127);
    } else {
        while (waitpid(pid, &status, 0) < 0) {
            status = -1;
            break;
        }
    }

    return status;
}
