#ifndef _KERNEL_UTILS_STRING_H
#define _KERNEL_UTILS_STRING_H

#include <stddef.h>
#include <stdint.h>

int memcmp(const void* ptr1, const void* ptr2, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t n);
char* strncpy(char* dest, const char* src, size_t n);
char* strdup(const char* str);
size_t strlen(const char* str);
char* basename(char* str);

#endif /* _KERNEL_UTILS_STRING_H */
