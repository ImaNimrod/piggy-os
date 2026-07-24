#ifndef _SH_H
#define _SH_H

extern char* cwd;
extern int last_status;

int execute(int argc, char* argv[]);
int split_args(char* line, char*** argv);

#endif /* _SH_H */
