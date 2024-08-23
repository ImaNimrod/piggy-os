#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>

#define NCCS 32

#define IGNCR   0001
#define ICRNL   0002
#define INLCR   0004

#define ECHO    0001
#define ECHOE   0002
#define ECHOCTL 0020
#define ICANON  0100  

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

int tcflush(int, int);
int tcgetattr(int, struct termios*);
int tcsetattr(int, const struct termios*);

#endif /* _TERMIOS_H */
