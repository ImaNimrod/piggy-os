#ifndef _KERNEL_NET_NETIF_H
#define _KERNEL_NET_NETIF_H 1

#include <net/packet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <utils/spinlock.h>
#include <utils/vector.h>

#define NETIF_FLAG_UP       (1 << 0)
#define NETIF_FLAG_DYNAMIC  (1 << 1)
#define NETIF_FLAG_RUNNING  (1 << 2)
#define NETIF_FLAG_LOOPBACK (1 << 7)

struct netif {
    int flags;
    uint16_t mtu;

    mac_address_t mac;
    ipv4_address_t ipv4_address;
    ipv4_address_t ipv4_gateway;
    ipv4_address_t ipv4_subnet_mask;

    void* device;

    vector_t* packet_queue;
    spinlock_t packet_queue_lock;

    size_t rx_count;
    size_t tx_count;

    spinlock_t send_lock;

    bool (*send_packet) (struct netif*, struct packet*);
    void (*update_flags) (struct netif*, uint16_t);
};

struct netif* netif_create(void);
bool netif_add_packet(struct netif* netif, const void* buf, uint16_t length);
bool netif_send_packet(struct netif* netif, struct packet* packet);
void net_init(void);

#endif /* _KERNEL_NET_NETIF_H */
