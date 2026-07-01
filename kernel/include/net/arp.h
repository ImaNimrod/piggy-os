#ifndef _KERNEL_NET_ARP_H
#define _KERNEL_NET_ARP_H

#include <net/eth.h>
#include <net/ipv4.h>

struct netif;

void arp_handle(struct netif* netif, const void* buf);
bool arp_lookup(struct netif* netif, ipv4_address_t ip, mac_address_t* mac);

void arp_init(void);

#endif /* _KERNEL_NET_ARP_H */
