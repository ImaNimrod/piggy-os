#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include <stddef.h>
#include <stdint.h>

int memcmp(const void* ptr1, const void* ptr2, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);

#endif /* _KERNEL_STRING_H */
