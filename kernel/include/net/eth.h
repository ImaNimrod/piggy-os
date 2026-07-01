#ifndef _KERNEL_NET_ETH_H
#define _KERNEL_NET_ETH_H

#include <stdint.h>

typedef uint8_t mac_address_t[6];

typedef enum : uint16_t {
    ETHERTYPE_ARP   = 0x0806,
    ETHERTYPE_IPV4  = 0x0800,
    ETHERTYPE_IPV6  = 0x86dd,
} ethertype_t;

struct eth_header {
    mac_address_t dmac;
    mac_address_t smac;
    ethertype_t ethertype;
    uint8_t payload[];
} __attribute__((packed));

struct netif;

extern mac_address_t BROADCAST_MAC;

void eth_handle(struct netif* netif, const void* buf);
void eth_populate_packet(struct netif* netif, mac_address_t* destination, ethertype_t type, void* buf);

#endif /* _KERNEL_NET_ETH_H */
