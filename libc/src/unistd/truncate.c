#include <unistd.h>

int truncate(const char* path, off_t length) {
    int fd = open(path, O_WRONLY | (length == 0 ? O_TRUNC : 0));
    if (fd < 0) {
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    int ret = ftruncate(fd, length);
    close(fd);
    return ret;
}
