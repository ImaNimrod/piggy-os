#include <fcntl.h>
#include <unistd.h>

int chdir(const char* path) {
    int fd = open(path, O_PATH);
    if (fd < 0) {
        return -1;
    }

    int ret = fchdir(fd);
    close(fd);
    return ret;
}
