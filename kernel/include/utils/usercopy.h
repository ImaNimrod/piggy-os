#ifndef _KERNEL_UTILS_USERCOPY_H
#define _KERNEL_UTILS_USERCOPY_H

#include <stddef.h>
#include <stdint.h>
#include <utils/string.h>

static inline int _memcpy_wrapper(void* restrict dest, const void* src, size_t n) {
    memcpy(dest, src, n);
    return 0;
}

static inline int _memset_wrapper(void* restrict dest, int c, size_t n) {
    memset(dest, c, n);
    return 0;
}

#define IS_USER_ADDRESS(addr) ((uintptr_t) (addr) < 0x0000800000000000 && addr != NULL)

#define USER_MEMCPY_MAYBE_FROM_USER(dest, src, n) IS_USER_ADDRESS((src)) ? user_memcpy_from_user((dest), (src), (n)) : _memcpy_wrapper((dest), (src), (n))
#define USER_MEMCPY_MAYBE_TO_USER(dest, src, n) IS_USER_ADDRESS((dest)) ? user_memcpy_to_user((dest), (src), (n)) : _memcpy_wrapper((dest), (src), (n))
#define USER_MEMSET_MAYBE_USER(dest, c, n) IS_USER_ADDRESS((dest)) ? user_memset((dest), (c), (n)) : _memset_wrapper((dest), (c), (n))

int user_memcpy_from_user(void* restrict dest, const void* restrict usrc, size_t n);
int user_memcpy_to_user(void* restrict udest, const void* restrict src, size_t n);
int user_memset(void* udest, int c, size_t n);
int user_strlen(const char* ustr, size_t* ret);

#endif /* _KERNEL_UTILS_USERCOPY_H */
