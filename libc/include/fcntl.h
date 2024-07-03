#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

#define O_PATH      0200
#define O_RDONLY    0000
#define O_WRONLY    0001
#define O_RDWR      0002

#define O_ACCMODE   (03 | O_PATH)

#define O_CREAT     0004
#define O_DIRECTORY 0010
#define O_TRUNC     0020
#define O_APPEND    0040
#define O_EXCL      0100
#define O_CLOEXEC   0200

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int creat(const char*, ...);
int fcntl(int, int, ...);
int open(const char*, int, ...);

#endif /* _FCNTL_H */
