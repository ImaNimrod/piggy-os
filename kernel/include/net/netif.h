#ifndef _KERNEL_NET_NETIF_H
#define _KERNEL_NET_NETIF_H

#include <net/eth.h>
#include <net/ipv4.h>
#include <stddef.h>

#define IFNAMSIZ 16

#define IFF_UP          (1 << 0)
#define IFF_BROADCAST   (1 << 1)
#define IFF_LOOPBACK    (1 << 3)
#define IFF_RUNNING     (1 << 6)
#define IFF_MULTICAST   (1 << 12)

#define MAC_ADDRESS_FORMAT          "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDRESS_PRINT(mac)      (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]

#define IPV4_ADDRESS(a, b, c, d)    (((ipv4_address_t) a << 24) | ((ipv4_address_t) b << 16) | ((ipv4_address_t) c << 8) | ((ipv4_address_t) d))
#define IPV4_FORMAT                 "%u.%u.%u.%u"
#define IPV4_PRINT(ip)              ((ip) >> 24) & 0xff, ((ip) >> 16) & 0xff, ((ip) >> 8) & 0xff, (ip) & 0xff

typedef enum {
    NETIF_TYPE_LO,
    NETIF_TYPE_ETH,
} netif_type_t;

struct packet {
    void* buf;
    size_t size;
    size_t offset;
};

struct netif {
    char name[IFNAMSIZ];
    netif_type_t type;

    short flags;
    uint16_t mtu;

    mac_address_t mac;
    ipv4_address_t ipv4_address;
    ipv4_address_t ipv4_mask;

    void* device;

    bool (*alloc_packet)(struct netif*, struct packet*, size_t);
    void (*free_packet)(struct netif*, struct packet*);
    bool (*send_packet)(struct netif*, struct packet*, mac_address_t*, ethertype_t);
    void (*update_flags)(struct netif*, uint16_t);

    struct netif* next;
};

struct netif* netif_find(const char* name);
void netif_register(struct netif* netif);

void net_init(void);

#endif /* _KERNEL_NET_NETIF_H */
