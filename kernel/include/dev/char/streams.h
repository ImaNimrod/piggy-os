#ifndef _KERNEL_DEV_CHAR_STREAMS_H
#define _KERNEL_DEV_CHAR_STREAMS_H

#include <stddef.h>
#include <stdint.h>

#define STREAM_MAJ          1

#define STREAM_NULL_MIN     0
#define STREAM_ZERO_MIN     1
#define STREAM_RANDOM_MIN   2

void streams_init(void);

#endif /* _KERNEL_DEV_CHAR_STREAMS_H */
