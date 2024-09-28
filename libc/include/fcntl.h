#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

#define F_DUPFD         0
#define F_DUPFD_CLOEXEC 1
#define F_GETFD         2
#define F_SETFD         3
#define F_GETFL         4
#define F_SETFL         5

#define O_PATH      00200
#define O_RDONLY    00000
#define O_WRONLY    00001
#define O_RDWR      00002

#define O_ACCMODE   (03 | O_PATH)

#define O_CREAT     00004
#define O_DIRECTORY 00010
#define O_TRUNC     00020
#define O_APPEND    00040
#define O_EXCL      00100
#define O_CLOEXEC   00400
#define O_NONBLOCK  01000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int creat(const char*, mode_t);
int fcntl(int, int, ...);
int open(const char*, int, ...);

#endif /* _FCNTL_H */
