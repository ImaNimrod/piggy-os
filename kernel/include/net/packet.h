#ifndef _KERNEL_NET_PACKET_H
#define _KERNEL_NET_PACKET_H 1

#include <stdint.h>

struct packet {
    struct netif* netif;
    uint16_t length;
    void* buf;
};

struct packet* packet_alloc(struct netif* netif, uint16_t length);
void packet_free(struct packet* packet);
void packet_init(void);

#endif /* _KERNEL_NET_PACKET_H */
