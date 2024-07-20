#ifndef _KERNEL_UTILS_USER_ACCESS_H
#define _KERNEL_UTILS_USER_ACCESS_H

#include <stdbool.h>
#include <stddef.h>

#define USER_ACCESS_BEGIN \
    do { \
        if (this_cpu()->smepsmap_enabled) { \
            stac(); \
        } \
    } while (0)

#define USER_ACCESS_END \
    do { \
        if (this_cpu()->smepsmap_enabled) { \
            clac(); \
        } \
    } while (0)

bool check_user_ptr(const void* ptr);
void* copy_from_user(void* kdest, const void* usrc, size_t size);
void* copy_to_user(void* udest, const void* ksrc, size_t size);

#endif /* _KERNEL_UTILS_USER_ACCESS_H */
