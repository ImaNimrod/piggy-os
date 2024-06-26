#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stddef.h>
#include <stdint.h>

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

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef int32_t pid_t;
typedef int32_t tid_t;

#endif /* _KERNEL_TYPES_H */
