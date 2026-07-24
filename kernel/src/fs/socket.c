#include <errno.h>
#include <fs/socket.h>
#include <mem/paging.h>
#include <mem/slab.h>
#include <net/raw.h>
#include <net/udp.h>
#include <sys/timer.h>
#include <utils/macros.h> 
#include <utils/usercopy.h> 

#define ARPHRD_ETHER 1

struct ifreq {
    char ifr_name[IFNAMSIZ];

    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short ifr_flags;
        int ifr_ivalue;
        int ifr_mtu;
    };
};

struct rtentry {
    char ifname[IFNAMSIZ];

    uint32_t destination;
    uint32_t gateway;
    uint32_t mask;

    uint32_t metric;
};

static int socket_ioctl(struct vfs_node* node, int request, void* argp);
static int socket_truncate(struct vfs_node* node, off_t length);
static short socket_poll(struct vfs_node* node, short events, struct poll_table* pt);
static int socket_sync(struct vfs_node* node);
static int socket_getstat(struct vfs_node* node, struct stat* stat);
static int socket_setstat(struct vfs_node* node, const struct stat* stat, int flags);
static int socket_lock(struct vfs_node* node);
static int socket_unlock(struct vfs_node* node);
static void socket_inactive(struct vfs_node* node);

static struct vfs_node_ops socket_node_ops = {
    .ioctl = socket_ioctl,
    .truncate = socket_truncate,
    .poll = socket_poll,
    .sync = socket_sync,
    .getstat = socket_getstat,
    .setstat = socket_setstat,
    .lock = socket_lock,
    .unlock = socket_unlock,
    .inactive = socket_inactive,
};

static ino_t inode_counter = 1;

static int socket_ioctl(struct vfs_node* node, int request, void* argp) {
    struct socket_node* snode = (struct socket_node*) node;

    int ret = 0;

    switch (request) {
        case SIOCGIFCONF:
            break;
        case SIOCGIFFLAGS:
        case SIOCSIFFLAGS:
        case SIOCGIFMTU:
        case SIOCSIFMTU:
        case SIOCGIFADDR:
        case SIOCSIFADDR:
        case SIOCGIFNETMASK:
        case SIOCSIFNETMASK:
        case SIOCGIFBRDADDR:
        case SIOCSIFBRDADDR:
        case SIOCGIFHWADDR:
        case SIOCGIFINDEX: {
            struct ifreq ifr;
            if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&ifr, argp, sizeof(ifr))) < 0) {
                break;
            }

            struct netif* netif = netif_find(ifr.ifr_name);
            if (!netif) {
                ret = -ENODEV;
                break;
            }

            switch (request) {
                case SIOCGIFFLAGS:
                    ifr.ifr_flags = netif->flags;
                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                case SIOCSIFFLAGS:
                    netif->flags = ifr.ifr_flags;
                    break;
                case SIOCGIFMTU:
                    ifr.ifr_mtu = netif->mtu;
                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                case SIOCSIFMTU:
                    ret = -ENOTSUP;
                    break;
                case SIOCGIFADDR: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    sin->sin_family = AF_INET;
                    sin->sin_addr.s_addr = htonl(netif->ipv4_addr);
                    sin->sin_port = 0;

                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                }
                case SIOCSIFADDR: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    if (sin->sin_family != AF_INET) {
                        ret = -EAFNOSUPPORT;
                        break;
                    }

                    netif->ipv4_addr = ntohl(sin->sin_addr.s_addr);
                    ret = ipv4_route_add(netif, netif->ipv4_addr, 0, netif->ipv4_mask);
                    break;
                }
                case SIOCGIFNETMASK: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    sin->sin_family = AF_INET;
                    sin->sin_addr.s_addr = htonl(netif->ipv4_mask);
                    sin->sin_port = 0;

                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                }
                case SIOCSIFNETMASK: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    if (sin->sin_family != AF_INET) {
                        ret = -EAFNOSUPPORT;
                        break;
                    }

                    netif->ipv4_mask = ntohl(sin->sin_addr.s_addr);
                    break;
                }
                case SIOCGIFBRDADDR: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    sin->sin_family = AF_INET;
                    sin->sin_addr.s_addr = htonl(netif->ipv4_addr | ~netif->ipv4_mask);
                    sin->sin_port = 0;

                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                }
                case SIOCSIFBRDADDR: {
                    struct sockaddr_in* sin = (struct sockaddr_in*) &ifr.ifr_addr;
                    if (sin->sin_family != AF_INET) {
                        ret = -EAFNOSUPPORT;
                        break;
                    }

                    ret = -ENOTSUP;
                    break;
                }
                case SIOCGIFINDEX:
                    ifr.ifr_ivalue = netif->index;
                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
                case SIOCGIFHWADDR:
                    memset(&ifr.ifr_hwaddr, 0, sizeof(ifr.ifr_hwaddr));

                    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
                    memcpy(ifr.ifr_hwaddr.sa_data, netif->mac, 6);

                    ret = USER_MEMCPY_MAYBE_TO_USER(argp, &ifr, sizeof(ifr));
                    break;
            }

            break;
        }
        case SIOCADDRT:
        case SIOCDELRT: {
            struct rtentry route;
            if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&route, argp, sizeof(route))) < 0) {
                break;
            }

            struct netif* netif = netif_find(route.ifname);
            if (!netif) {
                ret = -ENODEV;
                break;
            }

            if (request == SIOCADDRT) {
                ret = ipv4_route_add(netif, ntohl(route.destination), ntohl(route.gateway), ntohl(route.mask));
            } else {
                ret = ipv4_route_delete(ntohl(route.destination), ntohl(route.gateway), ntohl(route.mask));
            }

            break;
        }
        case SIOCIFBIND: {
            struct ifreq ifr;
            if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&ifr, argp, sizeof(ifr))) < 0) {
                break;
            }

            struct netif* netif = netif_find(ifr.ifr_name);
            if (!netif) {
                ret = -ENODEV;
                break;
            }

            snode->bound_netif = netif;
            break;
        }
        default:
            ret = -ENOTTY;
            break;
    }

    return ret;
}

static int socket_truncate(struct vfs_node* node, off_t length) {
    (void) node;
    (void) length;
    return -EPERM;
}

static short socket_poll(struct vfs_node* node, short events, struct poll_table* pt) {
    struct socket_node* snode = (struct socket_node*) node;
    return snode->sockops->poll(snode, events, pt);
}

static int socket_sync(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int socket_getstat(struct vfs_node* node, struct stat* stat) {
    return USER_MEMCPY_MAYBE_TO_USER((void*) stat, (const void*) &((struct socket_node*) node)->stat, sizeof(struct stat));
}

static int socket_setstat(struct vfs_node* node, const struct stat* stat, int flags) {
    (void) node;
    (void) stat;
    (void) flags;
    return -ENOTSUP;
}

static int socket_lock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static int socket_unlock(struct vfs_node* node) {
    (void) node;
    return 0;
}

static void socket_inactive(struct vfs_node* node) {
    struct socket_node* snode = (struct socket_node*) node;
    snode->sockops->destroy(snode);
}

int socket_create(int family, int type, int protocol, struct vfs_node** ret) {
    struct socket_node* node = NULL;

    int error = 0;

    switch (family) {
        case AF_UNSPEC:
            error = -EAFNOSUPPORT;
            break;
        case AF_INET:
            switch (type) {
                case SOCK_DGRAM:
                    if (protocol == 0 || protocol == IPPROTO_UDP) {
                        error = udp_socket_create(&node);
                    } else {
                        error = -EPROTONOSUPPORT;
                    }
                    break;
                case SOCK_RAW:
                    error = raw_socket_create(protocol, &node);
                    break;
                case SOCK_STREAM:
                default:
                    error = -EPROTONOSUPPORT;
                    break;
            }
            break;
        case AF_UNIX:
        default:
            error = -EINVAL;
            break;
    }

    if (error < 0) {
        return error;
    }

    node->type = VFS_TYPE_SOCKET;
    node->ops = &socket_node_ops;
    node->refcount = 1;

    node->stat.st_dev = 0;
    node->stat.st_ino = __atomic_fetch_add(&inode_counter, 1, __ATOMIC_SEQ_CST);
    node->stat.st_mode = vfs_type_to_mode(node->type);
    node->stat.st_nlink = 1;
    node->stat.st_rdev = 0;
    node->stat.st_size = 0;
    node->stat.st_blksize = PAGE_SIZE_4KB;
    node->stat.st_blocks = 0;
    node->stat.st_atim = node->stat.st_mtim = node->stat.st_ctim = time_realtime;

    *ret = (struct vfs_node*) node;
    return 0;
}

int socket_shutdown(struct socket_node* snode, int how) {
    if (how & ~SHUT_RDWR) {
        return -EINVAL;
    }

    mutex_acquire(&snode->mutex);

    snode->shutdown |= how;

    mutex_release(&snode->mutex);
    return 0;
}
