#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H 1

#include <stdint.h>

#define makedev(maj, min) (dev_t) ((((maj) << 8) & 0xff00u) | ((min) & 0x00ffu))
#define major(dev) (uint8_t) (((dev) & 0xff00u) >> 8)
#define minor(dev) (uint8_t) ((dev) & 0x00ffu)

typedef int32_t pid_t;
typedef int32_t tid_t;

typedef int64_t off_t;
typedef int64_t ssize_t;

typedef uint16_t dev_t;
typedef uint64_t ino_t;
typedef int32_t mode_t;

typedef int64_t blksize_t;
typedef int64_t blkcnt_t;

typedef int64_t time_t;

struct timespec {
    time_t tv_sec;
    time_t tv_nsec;
};

#define O_PATH      010000000
#define O_SEARCH    O_PATH

#define O_RDONLY    00000
#define O_WRONLY    00001
#define O_RDWR      00002

#define O_ACCMODE   (03 | O_SEARCH)

#define O_CREAT     00004
#define O_DIRECTORY 00010
#define O_TRUNC     00020
#define O_APPEND    00040
#define O_EXCL      00100
#define O_NONBLOCK  00400
#define O_CLOEXEC   01000

#define S_IFMT      0x0f000
#define S_IFBLK     0x01000
#define S_IFCHR     0x08000
#define S_IFREG     0x03000
#define S_IFDIR     0x05000

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

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

#define NCCS 32

#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VTIME     5
#define VMIN      6
#define VSWTC     7
#define VSTART    8
#define VSTOP     9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

#define OPOST  0000001
#define OLCUC  0000002
#define ONLCR  0000004
#define OCRNL  0000010
#define ONOCR  0000020
#define ONLRET 0000040
#define OFILL  0000100
#define OFDEL  0000200

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ECHOK  0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000400
#define ECHOCTL 0001000
#define ECHOPRT 0002000
#define ECHOKE 0004000
#define IEXTEN 0100000

#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017

#define CSIZE  0000060
#define CS5    0000000
#define CS6    0000020
#define CS7    0000040
#define CS8    0000060
#define CSTOPB 0000100
#define CREAD  0000200
#define PARENB 0000400
#define PARODD 0001000
#define HUPCL  0002000
#define CLOCAL 0004000

typedef uint8_t cc_t;
typedef uint32_t tcflag_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

typedef uint8_t mac_address_t[6];
typedef uint32_t ipv4_address_t;

#endif /* _KERNEL_TYPES_H */
