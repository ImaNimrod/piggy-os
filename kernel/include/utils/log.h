#ifndef _KERNEL_UTILS_LOG_H
#define _KERNEL_UTILS_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void klog(const char* fmt, ...);
__attribute__((noreturn)) void kpanic(const char* fmt, ...);

#endif /* _KERNEL_UTILS_LOG_H */
