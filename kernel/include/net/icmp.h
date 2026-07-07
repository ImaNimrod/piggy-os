#ifndef _KERNEL_NET_ICMP_H
#define _KERNEL_NET_ICMP_H

#include <net/ipv4.h> 

void icmp_handle(ipv4_address_t source, const void* buf, uint16_t len);

#endif /* _KERNEL_NET_ICMP_H */
