#include <fcntl.h>
#include <unistd.h>
#include "dirent_internal.h"

DIR* opendir(const char* path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }

    DIR* dirp = fdopendir(fd);
    if (dirp == NULL) {
        close(fd);
    }
    return dirp;
}
