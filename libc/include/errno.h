#ifndef _ERRNO_H
#define _ERRNO_H

#define EAGAIN  1
#define EBADF   2
#define ECHILD  3
#define EEXIST  4
#define EFAULT  5
#define EIO     6
#define EINVAL  7
#define EISDIR  8
#define ENFILE  9
#define ENOENT  10
#define ENOEXEC 11
#define ENOMEM  12
#define ENOSPC  13
#define ENOSYS  14
#define ENOTDIR 15
#define ENOTTY  16
#define EPERM   17
#define ESPIPE  18

extern int errno;

#endif /* _ERRNO_H */
