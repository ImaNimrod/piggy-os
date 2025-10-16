#ifndef _KERNEL_NET_ARP_H
#define _KERNEL_NET_ARP_H

#include <net/netif.h>
#include <net/packet.h>
#include <stdbool.h>

bool arp_lookup_ip(struct netif* netif, ipv4_address_t ip, mac_address_t* mac);
void arp_handle(struct packet* packet, void* l3_data);
void arp_init(void);

#endif /* _KERNEL_NET_ARP_H */
