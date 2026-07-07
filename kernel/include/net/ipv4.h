#ifndef _KERNEL_NET_IPV4_H
#define _KERNEL_NET_IPV4_H

#include <stddef.h>
#include <stdint.h>

#define BROADCAST_IPV4 ((ipv4_address_t) 0xffffffff)

typedef enum : uint8_t {
    IPV4_PROTOCOL_ICMP = 1,
    IPV4_PROTOCOL_TCP = 6,
    IPV4_PROTOCOL_UDP = 17,
} ipv4_protocol_t;

typedef uint32_t ipv4_address_t;

struct ipv4_header {
    uint8_t ihl: 4;
    uint8_t version: 4;
    uint8_t tos;
    uint16_t length;
    uint16_t identification;
    uint16_t flags: 3;
    uint16_t fragment_offset: 13;
    uint8_t ttl;
    ipv4_protocol_t protocol;
    uint16_t checksum;
    ipv4_address_t source;
    ipv4_address_t destination;
    uint8_t payload[];
} __attribute__((packed));

struct netif;

uint16_t inet_checksum(void* buf, uint16_t len);

bool ipv4_add_route(struct netif* netif, ipv4_address_t address, ipv4_address_t gateway, ipv4_address_t mask);
void ipv4_handle(struct netif* netif, const void* buf);
int ipv4_send(const void* buf, size_t len, ipv4_address_t destination, ipv4_protocol_t protocol, struct netif* broadcast_netif);

void ipv4_init(void);

#endif /* _KERNEL_NET_IPV4_H */
