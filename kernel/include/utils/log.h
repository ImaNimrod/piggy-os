#ifndef _KERNEL_UTILS_LOG_H
#define _KERNEL_UTILS_LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

extern spinlock_t print_lock;

void vklog(const char* fmt, va_list args);
void klog(const char* fmt, ...);

#endif /* _KERNEL_UTILS_LOG_H */
