#ifndef _KERNEL_UTILS_LOG_H
#define _KERNEL_UTILS_LOG_H

#include <cpu/isr.h>

void klog(const char* fmt, ...);
[[noreturn]] void kpanic(struct registers* r, bool stack_trace, const char* fmt, ...);

#endif /* _KERNEL_UTILS_LOG_H */
