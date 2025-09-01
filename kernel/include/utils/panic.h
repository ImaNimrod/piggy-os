#ifndef _KERNEL_UTILS_PANIC_H
#define _KERNEL_UTILS_PANIC_H 1

#include <cpu/isr.h>
#include <stdbool.h>
#include <utils/macros.h>

#define PANIC_IPI_VECTOR 253

NORETURN void kpanic(struct registers* r, bool stack_trace, const char* fmt, ...);

#endif /* _KERNEL_UTILS_PANIC_H */
