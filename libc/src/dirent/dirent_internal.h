#ifndef DIRENT_INTERNAL_H
#define DIRENT_INTERNAL_H

#include <dirent.h>

#define BUFFER_CAPACITY 1024

struct __DIR {
    int fd;
    ssize_t buffer_size;
    off_t offset;
    char buffer[BUFFER_CAPACITY];
};

#endif /* DIRENT_INTERNAL_H */
