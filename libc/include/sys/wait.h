#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

pid_t wait(int*);
pid_t waitpid(pid_t, int*, int);

#endif /* _SYS_WAIT_H */
