#ifndef _KERNEL_UTILS_RANDOM_H
#define _KERNEL_UTILS_RANDOM_H

#include <stddef.h>
#include <stdint.h>

void srand(uint64_t seed);
uint64_t rand(void);
size_t rand_fill(void* buf, size_t count);
void random_init(void);

#endif /* _KERNEL_UTILS_RANDOM_H */
