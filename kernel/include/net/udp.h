#ifndef _KERNEL_NET_UDP_H
#define _KERNEL_NET_UDP_H

#include <fs/socket.h>
#include <net/ipv4.h> 

void udp_handle(ipv4_address_t source, ipv4_address_t destination, const void* buf, uint16_t len);
int udp_socket_create(struct socket_node** ret);

void udp_init(void);

#endif /* _KERNEL_NET_UDP_H */
