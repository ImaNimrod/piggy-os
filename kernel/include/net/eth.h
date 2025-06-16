#ifndef _KERNEL_NET_ETH_H
#define _KERNEL_NET_ETH_H 1

#include <net/packet.h>
#include <stdint.h>
#include <types.h>

#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_IPV6  0x86dd

struct eth_header {
    mac_address_t dmac;
    mac_address_t smac;
    uint16_t ethertype;
    uint8_t payload[];
} __attribute__((packed));

extern mac_address_t broadcast_mac;

void eth_handle(struct packet* packet);
void* eth_populate_packet(struct packet* packet, mac_address_t* dmac, uint16_t ethertype);

#endif /* _KERNEL_NET_ETH_H */
