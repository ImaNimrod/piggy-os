#ifndef _UNISTD_H
#define _UNISTD_H

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define R_OK (1 << 0)
#define W_OK (1 << 1)
#define X_OK (1 << 2)

extern char** environ;

extern char* optarg;
extern int opterr, optind, optopt;

int chdir(const char*);
int close(int);
int dup(int);
int execl(const char*, const char*, ...);
int execlp(const char*, const char*, ...);
int execv(const char*, char* const[]);
int execve(const char*, char* const[], char* const[]);
int execvp(const char*, char* const[]);
__attribute__((noreturn)) void _exit(int);
int fchdir(int);
pid_t fork(void);
int fsync(int);
int ftruncate(int, off_t);
char* getcwd(char*, size_t);
int getopt(int, char* const[], const char*);
pid_t getpid(void);
pid_t getppid(void);
off_t lseek(int, off_t, int);
ssize_t read(int, void*, size_t);
void* sbrk(intptr_t);
int sleep(unsigned int);
int truncate(const char*, off_t);
int usleep(unsigned int);
ssize_t write(int, const void*, size_t);

#endif /* _UNISTD_H */
