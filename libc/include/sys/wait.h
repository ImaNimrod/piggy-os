#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG    (1 << 0)

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WCOREDUMP(s) ((s) & 0x80)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short) ((((s) & 0xffff) * 0x10001)>>8) > 0x7f00)
#define WIFSIGNALED(s) (((s) & 0xffff) -1U < 0xffu)
#define WIFCONTINUED(s) ((s) == 0xffff)

pid_t wait(int*);
pid_t waitpid(pid_t, int*, int);

#endif /* _SYS_WAIT_H */
