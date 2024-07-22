#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int stat(const char* path, struct stat* stat) {
    int fd = open(path, O_PATH);
    if (fd < 0) {
        return -1;
    }

    int ret = fstat(fd, stat);
    close(fd);
    return ret;
}
