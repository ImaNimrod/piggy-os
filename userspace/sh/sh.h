#ifndef _SH_H
#define _SH_H

#include <stdlib.h>

extern char cwd[PATH_MAX];
extern int last_status;

int execute(int argc, char* argv[]);
int split_args(char* line, char*** argv);

static inline void setpwd(void) {
    getcwd(cwd, sizeof(cwd));
    setenv("PWD", cwd, 1);
}

#endif /* _SH_H */
