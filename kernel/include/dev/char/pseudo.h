#ifndef _KERNEL_DEV_CHAR_PSEUDO_H
#define _KERNEL_DEV_CHAR_PSEUDO_H

#define PSEUDO_DEV_MAJOR        1
#define PSEUDO_DEV_NULL_MINOR   2
#define PSEUDO_DEV_ZERO_MINOR   3
#define PSEUDO_DEV_FULL_MINOR   4
#define PSEUDO_DEV_RANDOM_MINOR 5

void pseudo_dev_init(void);

#endif /* _KERNEL_DEV_CHAR_PSEUDO_H */
