#ifndef _KERNEL_UTILS_STRING_H
#define _KERNEL_UTILS_STRING_H

#include <stddef.h>
#include <stdint.h>

int memcmp(const void* ptr1, const void* ptr2, size_t n);
uint8_t* memcpy8(uint8_t* restrict dest, const uint8_t* restrict src, size_t n);
uint64_t* memcpy64(uint64_t* restrict dest, const uint64_t* restrict src, size_t n);
void* memcpy(void* restrict dest, const void* restrict src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
uint8_t* memset8(uint8_t* dest, uint8_t c, size_t n);
uint64_t* memset64(uint64_t* dest, uint64_t c, size_t n);
void* memset(void* dest, int c, size_t n);

int strcmp(const char* str1, const char* str2);
char* strdup(const char* str);
size_t strlen(const char* str);
int strncmp(const char* str1, const char* str2, size_t n);
char* strncpy(char* dest, const char* src, size_t n);

#endif /* _KERNEL_UTILS_STRING_H */
