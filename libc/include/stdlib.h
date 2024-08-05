#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_FAILURE (-1)
#define EXIT_SUCCESS 0

#define MB_CUR_MAX 4
#define RAND_MAX 0x7fffffff

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long int quot;
    long int rem;
} ldiv_t;

__attribute__((noreturn)) void abort(void);
int abs(int);
int atexit(void (*)(void));
int atoi(const char*);
long int atol(const char*);
long long int atoll(const char*);
void* bsearch(const void*, const void*, size_t, size_t, int (*)(const void*, const void*));
void* calloc(size_t, size_t);
div_t div(int, int);
__attribute__((noreturn)) void exit(int);
void free(void*);
char* getenv(const char*);
long int labs(long);
ldiv_t ldiv(long, long);
void* malloc(size_t);
int mblen(const char*, size_t);
size_t mbstowcs(wchar_t* __restrict, const char* __restrict, size_t);
int mbtowc(wchar_t* __restrict, const char* __restrict, size_t);
void qsort(void*, size_t, size_t, int (*)(const void*, const void*));
int rand(void);
void* realloc(void*, size_t);
void* reallocarray(void*, size_t, size_t);
int setenv(const char*, const char*, int);
void srand(unsigned int);
long int strtol(const char* __restrict, char** __restrict, int);
long long int strtoll(const char* __restrict, char** __restrict, int);
unsigned long int strtoul(const char* __restrict, char** __restrict, int);
unsigned long long int strtoull(const char* __restrict, char** __restrict, int);
int system(const char*);
int unsetenv(const char*);
size_t wcstombs(char* __restrict, const wchar_t* __restrict, size_t);
int wctomb(char*, wchar_t);

#endif /* _STDLIB_H */
