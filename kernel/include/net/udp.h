#ifndef _KERNEL_NET_UDP_H
#define _KERNEL_NET_UDP_H

#include <net/ipv4.h> 
#include <net/netif.h>

void udp_handle(struct netif* netif, ipv4_address_t source, const void* buf, size_t len);

#endif /* _KERNEL_NET_UDP_H */
