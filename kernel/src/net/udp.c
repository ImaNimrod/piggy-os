#include <errno.h>
#include <mem/slab.h>
#include <fs/socket.h>
#include <net/udp.h>
#include <utils/log.h> 
#include <utils/macros.h> 
#include <utils/mutex.h> 
#include <utils/spinlock.h> 
#include <utils/string.h> 
#include <utils/usercopy.h>
#include <utils/wait_queue.h>

struct udp_header {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct receive_data {
    ipv4_address_t addr;
    uint16_t port;

    void* buf;
    size_t len;

    struct receive_data* next;
};

struct udp_socket {
    struct socket_node;

    ipv4_address_t local_addr;
    uint16_t local_port;
    bool bound;

    ipv4_address_t remote_addr;
    uint16_t remote_port;
    bool connected;

    struct receive_data* rx_head;
    struct receive_data* rx_tail;

    struct wait_queue rx_wq;
};

static struct udp_socket** udp_port_map;
static spinlock_t udp_port_map_lock;

static uint16_t udp_port_last = 50000;

static int udp_bind(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len); 
static int udp_connect(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len); 
static ssize_t udp_recv(struct socket_node* node, void* buf, size_t count, struct sockaddr* addr, socklen_t* addr_len);
static ssize_t udp_send(struct socket_node* node, const void* buf, size_t count, const struct sockaddr* addr, socklen_t addr_len);
static ssize_t udp_getsockname(struct socket_node* node, struct sockaddr* addr);
static ssize_t udp_getpeername(struct socket_node* node, struct sockaddr* addr);
static short udp_poll(struct socket_node* node, short events, struct poll_table* pt);
static void udp_destroy(struct socket_node* node);

static struct socket_ops udp_sockops = {
    .bind = udp_bind,
    .connect = udp_connect,
    .recv = udp_recv,
    .send = udp_send,
    .getsockname = udp_getsockname,
    .getpeername = udp_getpeername,
    .poll = udp_poll,
    .destroy = udp_destroy,
};

static int alloc_port(struct udp_socket* socket, uint16_t* out_port) {
    bool int_state = spinlock_acquire_irqsave(&udp_port_map_lock);

    if (*out_port) {
        if (!udp_port_map[*out_port]) {
            udp_port_map[*out_port] = socket;
            spinlock_release_irqsave(&udp_port_map_lock, int_state);
            return 0;
        }

        spinlock_release_irqsave(&udp_port_map_lock, int_state);
        return -EADDRINUSE;
    }

    for (int i = 0; i < 10000; i++) {
        uint16_t port = udp_port_last++;

        if (udp_port_last < 50000) {
            udp_port_last = 50000;
        }

        if (port < 50000 || port >= 60000) {
            continue;
        }

        if (!udp_port_map[port]) {
            udp_port_map[port] = socket;
            *out_port = port;

            spinlock_release_irqsave(&udp_port_map_lock, int_state);
            return 0;
        }
    }

    spinlock_release_irqsave(&udp_port_map_lock, int_state);
    return -EADDRINUSE;
}

static int udp_bind(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len) {
    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }
    if (addr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    int ret = 0;

    if (socket->bound) {
        ret = -EINVAL;
        goto end;
    }

    const struct sockaddr_in* in = (const struct sockaddr_in*) addr;

    ipv4_address_t address = ntohl(in->sin_addr.s_addr);
    uint16_t port = ntohs(in->sin_port);

    if ((ret = alloc_port(socket, &port)) < 0) {
        goto end;
    }

    socket->local_addr = address;
    socket->local_port = port;
    socket->bound = true;

end:
    mutex_release(&socket->mutex);
    return ret;
}

static int udp_connect(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len) {
    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }
    if (addr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    int ret = 0;

    if (socket->remote_port != 0) {
        ret = -EISCONN;
        goto end;
    }

    const struct sockaddr_in* in = (const struct sockaddr_in*) addr;

    ipv4_address_t address = ntohl(in->sin_addr.s_addr);
    uint16_t port = ntohs(in->sin_port);

    socket->remote_addr = address;
    socket->remote_port = port;
    socket->connected = true;

end:
    mutex_release(&socket->mutex);
    return ret;
}

static ssize_t udp_recv(struct socket_node* node, void* buf, size_t count, struct sockaddr* addr, socklen_t* addr_len) {
    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    while (!socket->rx_head) {
        mutex_release(&socket->mutex);

        int ret = wait_queue_wait(&socket->rx_wq);
        if (ret < 0) {
            return ret;
        }

        mutex_acquire(&socket->mutex);
    }

    struct receive_data* data = socket->rx_head;

    socket->rx_head = data->next;

    if (!socket->rx_head) {
        socket->rx_tail = NULL;
    }

    ssize_t copied = (ssize_t) MIN(data->len, count);

    ssize_t ret = USER_MEMCPY_MAYBE_TO_USER(buf, data->buf, copied);
    if (ret < 0) {
        goto end;
    }

    if (addr && addr_len) {
        socklen_t sin_len = 0;
        if ((ret = USER_MEMCPY_MAYBE_FROM_USER(&sin_len, addr_len, sizeof(socklen_t))) < 0) {
            goto end;
        }

        if (sin_len < sizeof(struct sockaddr_in)) {
            ret = -EINVAL;
            goto end;
        }

        struct sockaddr_in sin = {};
        sin.sin_family = AF_INET;
        sin.sin_port = data->port;
        sin.sin_addr.s_addr = data->addr;

        if ((ret = USER_MEMCPY_MAYBE_TO_USER(addr, &sin, MIN(sizeof(struct sockaddr_in), sin_len))) < 0) {
            goto end;
        }

        sin_len = sizeof(struct sockaddr_in);

        if ((ret = USER_MEMCPY_MAYBE_TO_USER(addr_len, &sin_len, sizeof(socklen_t))) < 0) {
            goto end;
        }

    }

    ret = copied;

end:
    kfree(data->buf);
    kfree(data);

    mutex_release(&socket->mutex);
    return ret;
}

static ssize_t udp_send(struct socket_node* node, const void* buf, size_t count, const struct sockaddr* addr, socklen_t addr_len) {
    struct udp_socket* socket = (struct udp_socket*) node;

    int ret = 0;

    mutex_acquire(&socket->mutex);

    if (socket->shutdown & SHUT_WR) {
        mutex_release(&socket->mutex);
        return 0;
    }

    ipv4_address_t dest_addr;
    uint16_t dest_port;

    if (!addr) {
        if (addr_len != 0) {
            mutex_release(&socket->mutex);
            return -EINVAL;
        }
        if (!socket->connected) {
            mutex_release(&socket->mutex);
            return -EDESTADDRREQ;
        }

        dest_addr = socket->remote_addr;
        dest_port = socket->remote_port;
    } else {
        if (addr_len < sizeof(struct sockaddr_in)) {
            mutex_release(&socket->mutex);
            return -EINVAL;
        }
        if (addr->sa_family != AF_INET) {
            mutex_release(&socket->mutex);
            return -EAFNOSUPPORT;
        }

        const struct sockaddr_in* in = (const struct sockaddr_in*) addr;

        dest_addr = ntohl(in->sin_addr.s_addr);
        dest_port = ntohs(in->sin_port);
    }

    if (!socket->bound) {
        if ((ret = alloc_port(socket, &socket->local_port)) < 0) {
            mutex_release(&socket->mutex);
            return ret;
        }

        socket->bound = true;
    }

    size_t total_count = sizeof(struct udp_header) + count;

    void* datagram = kmalloc(total_count);
    if (unlikely(!datagram)) {
        mutex_release(&socket->mutex);
        return -ENOMEM;
    }

    struct udp_header* header = datagram;
    header->src_port = htons(socket->local_port);
    header->dest_port = htons(dest_port);
    header->length = htons(total_count);
    header->checksum = 0;

    if ((ret = USER_MEMCPY_MAYBE_FROM_USER((void*) (header + 1), buf, count)) < 0) {
        mutex_release(&socket->mutex);
        kfree(datagram);
        return ret;
    }

    mutex_release(&socket->mutex);

    ret = ipv4_send(datagram, total_count, dest_addr, IPV4_PROTOCOL_UDP, socket->bound_netif);

    kfree(datagram);

    if (ret < 0) {
        return ret;
    }

    return count;
}

static ssize_t udp_getsockname(struct socket_node* node, struct sockaddr* addr) {
    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    struct sockaddr_in* sin = (struct sockaddr_in*) addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(socket->remote_addr);
    sin->sin_port = htons(socket->remote_port);

    mutex_release(&socket->mutex);

    return sizeof(struct sockaddr_in);
}

static ssize_t udp_getpeername(struct socket_node* node, struct sockaddr* addr) {
    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    struct sockaddr_in* sin = (struct sockaddr_in*) addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(socket->remote_addr);
    sin->sin_port = htons(socket->remote_port);

    mutex_release(&socket->mutex);

    return sizeof(struct sockaddr_in);
}

static short udp_poll(struct socket_node* node, short events, struct poll_table* pt) {
    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    short revents = 0;

    if (events & POLLIN) {
        if (socket->rx_head) {
            revents |= POLLIN;
        } else {
            poll_table_add(pt, &socket->rx_wq);
        }
    }

    if (events & POLLOUT) {
        revents |= POLLOUT;
    }

    mutex_release(&socket->mutex);

    return revents;
}

static void udp_destroy(struct socket_node* node) {
    struct udp_socket* socket = (struct udp_socket*) node;

    mutex_acquire(&socket->mutex);

    if (socket->local_port != 0) {
        bool int_state = spinlock_acquire_irqsave(&udp_port_map_lock);
        udp_port_map[socket->local_port] = NULL;
        spinlock_release_irqsave(&udp_port_map_lock, int_state);
    }

    kfree(socket);
}

void udp_handle(ipv4_address_t source, const void* buf, uint16_t len) {
    if (unlikely(len < sizeof(struct udp_header))) {
        return;
    }

    struct udp_header* header = (void*) buf;

    // TODO: verify UDP packet checksums

    uint16_t dest_port = ntohs(header->dest_port);
    uint16_t data_len = ntohs(header->length) - sizeof(struct udp_header);

    bool int_state = spinlock_acquire_irqsave(&udp_port_map_lock);

    struct udp_socket* socket = udp_port_map[dest_port];
    if (socket && !(socket->shutdown & SHUT_RD)) {
        VFS_NODE_REF(socket);
    }

    spinlock_release_irqsave(&udp_port_map_lock, int_state);

    if (!socket || (socket->shutdown & SHUT_RD)) {
        return;
    }

    struct receive_data* data = kmalloc(sizeof(struct receive_data));
    if (!data) {
        return;
    }
    data->addr = htonl(source);
    data->port = header->src_port;
    data->len = data_len;
    data->next = NULL;

    data->buf = kmalloc(data_len);
    if (!data->buf) {
        kfree(data);
        return;
    }

    memcpy(data->buf, (const void*) (header + 1), data->len);

    mutex_acquire(&socket->mutex);

    if (socket->rx_tail) {
        socket->rx_tail->next = data;
    } else {
        socket->rx_head = data;
    }

    socket->rx_tail = data;

    mutex_release(&socket->mutex);

    VFS_NODE_UNREF(socket);

    wait_queue_wake_all(&socket->rx_wq);
}

int udp_socket_create(struct socket_node** ret) {
    struct udp_socket* socket = kmallocz(sizeof(struct udp_socket));
    if (unlikely(!socket)) {
        return -ENOMEM;
    }

    socket->sockops = &udp_sockops;

    wait_queue_init(&socket->rx_wq);

    mutex_init(&socket->mutex);

    *ret = (struct socket_node*) socket;
    return 0;
}

void udp_init(void) {
    udp_port_map = kmallocz(65535 * sizeof(struct udp_socket*));
    if (unlikely(!udp_port_map)) {
        kpanic(NULL, false, "failed to create UDP port map");
    }

    spinlock_init(&udp_port_map_lock);
}
