#ifndef _KERNEL_DEV_CHAR_PSEUDO_H
#define _KERNEL_DEV_CHAR_PSEUDO_H

#include <stddef.h>
#include <stdint.h>

#define PSEUDO_MAJ          1

#define PSEUDO_NULL_MIN     0
#define PSEUDO_ZERO_MIN     1
#define PSEUDO_RANDOM_MIN   2

void pseudo_init(void);

#endif /* _KERNEL_DEV_CHAR_PSEUDO_H */
