#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stdint.h>

#define MAX_FDS 32
#define PATH_MAX 4096

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
#define O_NONBLOCK  00400
#define O_CLOEXEC   01000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_DUPFD         0
#define F_DUPFD_CLOEXEC 1
#define F_GETFD         2
#define F_SETFD         3
#define F_GETFL         4
#define F_SETFL         5

#define S_IFMT      0x0f000
#define S_IFBLK     0x01000
#define S_IFCHR     0x08000
#define S_IFREG     0x03000
#define S_IFDIR     0x05000

#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#define DT_UNKNOWN  0
#define DT_REG      1
#define DT_DIR      2
#define DT_CHR      3
#define DT_BLK      4

#define WNOHANG     (1 << 0)

#define makedev(maj, min) (dev_t) ((((maj) << 8) & 0xff00u) | ((min) & 0x00ffu))
#define major(dev) (uint8_t) (((dev) & 0xff00u) >> 8)
#define minor(dev) (uint8_t) ((dev) & 0x00ffu)

#define NCCS        32
#define VINTR       0
#define VQUIT       1
#define VERASE      2
#define VKILL       3
#define VEOF        4
#define VTIME       5
#define VMIN        6
#define VSWTC       7
#define VSTART      8
#define VSTOP       9
#define VSUSP       10
#define VEOL        11
#define VREPRINT    12
#define VDISCARD    13
#define VWERASE     14
#define VLNEXT      15
#define VEOL2       16

/* termios c_iflag options */
#define IGNCR   0001
#define ICRNL   0002
#define INLCR   0004

/* termios c_oflag options */

/* termios c_lflag options */
#define ECHO    0001
#define ECHOE   0002
#define ECHOCTL 0020
#define ICANON  0100  

#define SYSACT_MAGIC1   0xffbe6384abc6103b
#define SYSACT_MAGIC2   0xbec81abcf0998367
#define SYSACT_MAGIC3   0x9187ba8c7983f198
#define SYSACT_HALT     0
#define SYSACT_REBOOT   1
#define SYSACT_SHUTDOWN 2

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef int32_t pid_t;
typedef int32_t tid_t;

typedef uint16_t dev_t;
typedef uint64_t ino_t;
typedef uint16_t reclen_t;

typedef int32_t mode_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;

typedef uint32_t tcflag_t;
typedef uint32_t cc_t;

typedef int64_t time_t;

#define CLOCK_REALTIME              0
#define CLOCK_MONOTONIC             1
#define CLOCK_PROCESS_CPUTIME_ID    2
#define CLOCK_THREAD_CPUTIME_ID     3

struct timespec {
    time_t tv_sec;
    time_t tv_nsec;
};

typedef int32_t clockid_t;

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

struct dirent {
    ino_t d_ino;
    reclen_t d_reclen;
    unsigned char d_type;
    char d_name[];
};

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

struct utsname {
    char sysname[64];
    char nodename[100];
    char release[64];
    char version[64];
    char machine[64];
};

#endif /* _KERNEL_TYPES_H */
