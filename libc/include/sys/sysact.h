#ifndef _SYS_SYSACT_H
#define _SYS_SYSACT_H

#define SYSACT_MAGIC1   0xffbe6384abc6103b
#define SYSACT_MAGIC2   0xbec81abcf0998367
#define SYSACT_MAGIC3   0x9187ba8c7983f198

#define SYSACT_HALT     0
#define SYSACT_REBOOT   1
#define SYSACT_SHUTDOWN 2

void sysact(unsigned long int, unsigned long int, unsigned long int, int);

#endif /* _SYS_SYSACT_H */
