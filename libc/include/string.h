#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void* memchr(const void*, int, size_t);
int memcmp(const void*, const void*, size_t);
void* memcpy(void* __restrict, const void* __restrict, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);

char* stpcpy(char* __restrict str1, const char* __restrict str2);
char* stpncpy(char* __restrict str1, const char* __restrict str2, size_t n);
char* strcat(char* __restrict, const char* __restrict);
char* strncat(char* __restrict, const char* __restrict, size_t);
char* strchr(const char*, int);
char* strrchr(const char*, int);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, size_t);
int strcoll(const char* str1, const char* str2);
char* strcpy(char* __restrict, const char* __restrict);
char* strncpy(char* __restrict, const char* __restrict, size_t);
size_t strlen(const char*);
size_t strnlen(const char*, size_t);
char* strpbrk(const char*, const char*);
size_t strspn(const char*, const char*);
size_t strcspn(const char*, const char*);
char* strstr(const char*, const char*);
char* strtok(char* __restrict, const char* __restrict);
char* strtok_r(char*, const char*, char**);
size_t strxfrm(char* __restrict, const char* __restrict, size_t);

#endif /* _STRING_H */
