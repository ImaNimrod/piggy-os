#ifndef _KERNEL_FS_STREAMS_H
#define _KERNEL_FS_STREAMS_H

#define STREAM_DEV_MAJOR        1
#define STREAM_DEV_NULL_MINOR   3
#define STREAM_DEV_ZERO_MINOR   5
#define STREAM_DEV_FULL_MINOR   7

void streams_init(void);

#endif /* _KERNEL_FS_STREAMS_H */
