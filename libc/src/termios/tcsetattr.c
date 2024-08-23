#include <sys/ioctl.h>
#include <termios.h>

#define IOCTL_TTYSETATTR 0x3502

int tcsetattr(int fd, const struct termios* attr) {
    return ioctl(fd, IOCTL_TTYSETATTR, attr);
}
