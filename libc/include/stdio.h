#ifndef _STDIO_H
#define _STDIO_H

#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define BUFSIZ 4096
#define EOF (-1)

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef struct __FILE FILE;
typedef off_t fpos_t;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

void clearerr(FILE*);
int fclose(FILE*);
FILE* fdopen(int fd, const char* __restrict);
int feof(FILE*);
int ferror(FILE*);
int fflush(FILE*);
int fgetc(FILE*);
int fgetpos(FILE* __restrict, fpos_t* __restrict);
char* fgets(char* __restrict, int, FILE* __restrict);
FILE* fopen(const char* __restrict, const char* __restrict);
int fprintf(FILE* __restrict, const char* __restrict, ...);
int fputc(int, FILE*);
int fputs(const char* __restrict, FILE* __restrict);
size_t fread(void* __restrict, size_t, size_t, FILE* __restrict);
FILE* freopen(const char* __restrict, const char* __restrict, FILE* __restrict);
int fscanf(FILE* __restrict, const char* __restrict, ...);
int fseek(FILE*, long, int);
int fsetpos(FILE* __restrict, const fpos_t* __restrict);
long ftell(FILE*);
size_t fwrite(const void* __restrict, size_t, size_t, FILE* __restrict);
int getc(FILE*);
int getchar(void);
void perror(const char*);
int printf(const char* __restrict, ...);
int putc(int, FILE*);
int putchar(int);
int puts(const char*);
void rewind(FILE*);
int scanf(const char* __restrict, ...);
void setbuf(FILE* __restrict, char* __restrict);
int setvbuf(FILE* __restrict, char* __restrict, int, size_t);
int sprintf(char* __restrict, const char* __restrict, ...);
int snprintf(char* __restrict, size_t, const char* __restrict, ...);
int sscanf(const char* __restrict, const char* __restrict, ...);
FILE* tmpfile(void);
int ungetc(int, FILE*);
int vfprintf(FILE* __restrict, const char* __restrict, __gnuc_va_list);
int vfscanf(FILE* __restrict, const char* __restrict, __gnuc_va_list);
int vsscanf(const char* __restrict, const char* __restrict, __gnuc_va_list);
int vprintf(const char* __restrict, __gnuc_va_list);
int vsprintf(char* __restrict, const char* __restrict, __gnuc_va_list);
int vsnprintf(char* __restrict, size_t, const char* __restrict, __gnuc_va_list);

#endif /* _STDIO_H */
