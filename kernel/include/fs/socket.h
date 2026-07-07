#ifndef _KERNEL_FS_SOCKET_H
#define _KERNEL_FS_SOCKET_H

#include <fs/poll.h>
#include <fs/vfs.h>
#include <net/netif.h>
#include <utils/mutex.h>

#define AF_UNSPEC   0
#define AF_INET     1
#define AF_UNIX     2

#define SOCK_DGRAM  1
#define SOCK_RAW    2
#define SOCK_STREAM 3

#define IPPROTO_ICMP    1
#define IPPROTO_UDP     17

#define SHUT_RD     (1 << 0)
#define SHUT_WR     (1 << 1)
#define SHUT_RDWR   (SHUT_RD | SHUT_WR)

#define SIOCGIFNAME     0x8910
#define SIOCGIFCONF     0x8912
#define SIOCGIFFLAGS    0x8913
#define SIOCSIFFLAGS    0x8914
#define SIOCGIFMTU      0x8921
#define SIOCSIFMTU      0x8922
#define SIOCGIFADDR     0x8923
#define SIOCSIFADDR     0x8924
#define SIOCGIFNETMASK  0x8925
#define SIOCSIFNETMASK  0x8926
#define SIOCGIFHWADDR   0x8927
#define SIOCGIFINDEX    0x8933
#define SIOCADDRT       0x8958
#define SIOCDELRT       0x8959
#define SIOCIFBIND      0x895A

struct socket_node;

struct socket_ops {
    int (*bind)(struct socket_node*, const struct sockaddr*, socklen_t);
    int (*connect)(struct socket_node*, const struct sockaddr*, socklen_t);
    ssize_t (*recv)(struct socket_node*, void*, size_t, struct sockaddr*, socklen_t*);
    ssize_t (*send)(struct socket_node*, const void*, size_t, const struct sockaddr*, socklen_t);
    ssize_t (*getpeername)(struct socket_node*, struct sockaddr*);
    ssize_t (*getsockname)(struct socket_node*, struct sockaddr*);
    short (*poll)(struct socket_node*, short, struct poll_table*);
    void (*destroy)(struct socket_node*);
};

struct socket_node {
    struct vfs_node;
    struct stat stat;

    struct socket_ops* sockops;

    int shutdown;
    struct netif* bound_netif;

    mutex_t mutex;
};

int socket_create(int family, int type, int protocol, struct vfs_node** ret);
int socket_shutdown(struct socket_node* snode, int how);

#endif /* _KERNEL_FS_SOCKET_H */ 
