#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define O_RDONLY    0x00
#define O_WRONLY    0x01
#define O_RDWR      0x02
#define O_CREAT     0x04
#define O_DIRECTORY 0x08
#define O_PATH      0x10
#define O_TRUNC     0x20

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef int32_t pid_t;
typedef int32_t tid_t;

#endif /* _KERNEL_TYPES_H */
