#ifndef _KERNEL_NET_NETIF_H
#define _KERNEL_NET_NETIF_H

#include <net/eth.h>
#include <net/ipv4.h>
#include <stddef.h>

#define NETIF_FLAG_UP       (1 << 0)
#define NETIF_FLAG_DYNAMIC  (1 << 1)
#define NETIF_FLAG_RUNNING  (1 << 2)
#define NETIF_FLAG_LOOPBACK (1 << 7)

#define MAC_ADDRESS_FORMAT          "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDRESS_PRINT(mac)      (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]

#define IPV4_ADDRESS(a, b, c, d)    (((ipv4_address_t) a << 24) | ((ipv4_address_t) b << 16) | ((ipv4_address_t) c << 8) | ((ipv4_address_t) d))
#define IPV4_FORMAT                 "%u.%u.%u.%u"
#define IPV4_PRINT(ip)              ((ip) >> 24) & 0xff, ((ip) >> 16) & 0xff, ((ip) >> 8) & 0xff, (ip) & 0xff

struct packet {
    void* buf;
    size_t size;
    size_t offset;
};

struct netif {
    int flags;
    uint16_t mtu;

    mac_address_t mac;
    ipv4_address_t ipv4_address;
    ipv4_address_t ipv4_gateway;
    ipv4_address_t ipv4_subnet_mask;

    void* device;

    bool (*alloc_packet)(struct netif*, struct packet*, size_t);
    void (*free_packet)(struct netif*, struct packet*);
    bool (*send_packet)(struct netif*, struct packet*, mac_address_t*, ethertype_t);
    void (*update_flags)(struct netif*, uint16_t);
};

void net_init(void);

#endif /* _KERNEL_NET_NETIF_H */
