#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H 1

#include <stdint.h>

typedef int32_t pid_t;
typedef int32_t tid_t;

typedef int64_t time_t;

struct timespec {
    time_t tv_sec;
    time_t tv_nsec;
};

typedef uint8_t mac_address_t[6];
typedef uint32_t ipv4_address_t;

#endif /* _KERNEL_TYPES_H */
