#ifndef _KERNEL_NET_IPV4_H
#define _KERNEL_NET_IPV4_H

#include <net/packet.h>
#include <stdint.h>

#define IPV4_PROTOCOL_ICMP  1
#define IPV4_PROTOCOL_TCP   6
#define IPV4_PROTOCOL_UDP   17

struct ipv4_header {
    uint8_t ihl: 4;
    uint8_t version: 4;
    uint8_t tos;
    uint16_t length;
    uint16_t identification;
    uint16_t flags: 3;
    uint16_t fragment_offset: 13;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    ipv4_address_t source;
    ipv4_address_t destination;
    uint8_t payload[];
} __attribute__((packed));

uint16_t inet_checksum(void* data, uint16_t length);

void ipv4_handle(struct packet* packet, void* l3_data);
struct packet* ipv4_prepare_packet(struct netif* netif, uint16_t l4_size, uint8_t protocol, ipv4_address_t destination);

#endif /* _KERNEL_NET_IPV4_H */
