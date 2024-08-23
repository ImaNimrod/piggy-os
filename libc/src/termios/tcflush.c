#include <sys/ioctl.h>
#include <termios.h>

#define IOCTL_TTYFLUSH 0x3500

int tcflush(int fd, int selector) {
    return ioctl(fd, IOCTL_TTYFLUSH, selector);
}
