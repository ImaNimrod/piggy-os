#ifndef _KERNEL_UTILS_LOG_H
#define _KERNEL_UTILS_LOG_H

#include <cpu/isr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void klog(const char* fmt, ...);
__attribute__((noreturn)) void kpanic(struct registers* r, bool trace_stack, const char* fmt, ...);

#endif /* _KERNEL_UTILS_LOG_H */
