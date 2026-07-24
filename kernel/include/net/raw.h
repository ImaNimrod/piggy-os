#ifndef _KERNEL_NET_RAW_H
#define _KERNEL_NET_RAW_H

#include <fs/socket.h>
#include <net/ipv4.h> 

void raw_socket_receive(const struct ipv4_header* header, ipv4_address_t source, ipv4_address_t destination);
int raw_socket_create(ipv4_protocol_t protocol, struct socket_node** ret);

void raw_socket_init(void);

#endif /* _KERNEL_NET_RAW_H */
