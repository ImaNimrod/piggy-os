#ifndef _ERRNO_H
#define _ERRNO_H

#define EAGAIN          1
#define EBADF           2
#define ECHILD          3
#define EEXIST          4
#define EFAULT          5
#define EIO             6
#define EINVAL          7
#define EISDIR          8
#define EMFILE          9
#define ENAMETOOLONG    10
#define ENOENT          11
#define ENOEXEC         12
#define ENOMEM          13
#define ENOSPC          14
#define ENOSYS          15
#define ENOTDIR         16
#define ENOTTY          17
#define EPERM           18
#define ESPIPE          19

#define EOVERFLOW       20
#define ERANGE          21

extern int errno;

#endif /* _ERRNO_H */
