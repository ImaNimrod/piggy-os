#ifndef _KERNEL_DEV_BLOCK_VIRTIO_BLK_H
#define _KERNEL_DEV_BLOCK_VIRTIO_BLK_H

#include <dev/virtio.h>

#define VIOBLK_DEV_MAJOR 17

void virtio_blk_init(struct virtio_device* vio_dev);

#endif /* _KERNEL_DEV_BLOCK_VIRTIO_BLK_H */
