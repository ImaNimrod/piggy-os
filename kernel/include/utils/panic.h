#ifndef _KERNEL_UTILS_PANIC_H
#define _KERNEL_UTILS_PANIC_H

#include <cpu/isr.h>
#include <stdbool.h>

__attribute__((noreturn)) void kpanic(struct registers* r, bool trace_stack, const char* fmt, ...);

#endif /* _KERNEL_UTILS_PANIC_H */
