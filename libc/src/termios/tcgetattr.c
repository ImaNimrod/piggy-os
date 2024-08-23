#include <sys/ioctl.h>
#include <termios.h>

#define IOCTL_TTYGETATTR 0x3501

int tcgetattr(int fd, struct termios* attr) {
    return ioctl(fd, IOCTL_TTYGETATTR, attr);
}
