#ifndef _KERNEL_NET_PACKET_H
#define _KERNEL_NET_PACKET_H

#include <stdint.h>

typedef uint8_t mac_address_t[6];
typedef uint32_t ipv4_address_t;

struct packet {
    struct netif* netif;
    uint16_t length;
    void* buf;
};

struct packet* packet_alloc(struct netif* netif, uint16_t length);
void packet_free(struct packet* packet);
void packet_init(void);

#endif /* _KERNEL_NET_PACKET_H */
