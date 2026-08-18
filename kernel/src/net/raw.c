#include <errno.h>
#include <mem/slab.h>
#include <net/raw.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/spinlock.h>
#include <utils/usercopy.h>
#include <utils/wait_queue.h>

struct receive_data {
    ipv4_address_t addr;

    void* buf;
    size_t len;

    struct receive_data* next;
};

struct raw_socket {
    struct socket_node;

    ipv4_protocol_t protocol;

    ipv4_address_t local_addr;
    bool bound;

    ipv4_address_t remote_addr;
    bool connected;

    struct receive_data* rx_head;
    struct receive_data* rx_tail;

    struct wait_queue rx_wq;

    struct raw_socket* prev;
    struct raw_socket* next;
};

static struct raw_socket* raw_socket_list;
static spinlock_t raw_socket_list_lock;

static int raw_bind(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len); 
static int raw_connect(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len); 
static ssize_t raw_recv(struct socket_node* node, void* buf, size_t count, struct sockaddr* addr, socklen_t* addr_len);
static ssize_t raw_send(struct socket_node* node, const void* buf, size_t count, const struct sockaddr* addr, socklen_t addr_len);
static ssize_t raw_getsockname(struct socket_node* node, struct sockaddr* addr, socklen_t addr_len);
static ssize_t raw_getpeername(struct socket_node* node, struct sockaddr* addr, socklen_t addr_len);
static int raw_shutdown(struct socket_node* node, int how);
static short raw_poll(struct socket_node* node, short events, struct poll_table* pt);
static void raw_destroy(struct socket_node* node);

static struct socket_ops raw_sockops = {
    .bind = raw_bind,
    .connect = raw_connect,
    .recv = raw_recv,
    .send = raw_send,
    .getsockname = raw_getsockname,
    .getpeername = raw_getpeername,
    .shutdown = raw_shutdown,
    .poll = raw_poll,
    .destroy = raw_destroy,
};

static int raw_bind(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len) {
    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }
    if (addr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    struct raw_socket* socket = (struct raw_socket*) node;

    mutex_acquire(&socket->mutex);

    int ret = 0;

    if (socket->bound) {
        ret = -EINVAL;
        goto end;
    }

    const struct sockaddr_in* in = (const struct sockaddr_in*) addr;

    socket->local_addr = ntohl(in->sin_addr.s_addr);
    socket->bound = true;

end:
    mutex_release(&socket->mutex);
    return ret;
}

static int raw_connect(struct socket_node* node, const struct sockaddr* addr, socklen_t addr_len) {
    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }
    if (addr->sa_family != AF_INET) {
        return -EAFNOSUPPORT;
    }

    struct raw_socket* socket = (struct raw_socket*) node;

    mutex_acquire(&socket->mutex);

    const struct sockaddr_in* in = (const struct sockaddr_in*) addr;
    socket->remote_addr = ntohl(in->sin_addr.s_addr);
    socket->connected = true;

    mutex_release(&socket->mutex);
    return 0;
}

static ssize_t raw_recv(struct socket_node* node, void* buf, size_t count, struct sockaddr* addr, socklen_t* addr_len) {
    struct raw_socket* socket = (struct raw_socket*) node;

    mutex_acquire(&socket->mutex);

    while (!socket->rx_head) {
        mutex_release(&socket->mutex);

        int ret = wait_queue_wait(&socket->rx_wq, true);
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

static ssize_t raw_send(struct socket_node* node, const void* buf, size_t count, const struct sockaddr* addr, socklen_t addr_len) {
    struct raw_socket* socket = (struct raw_socket*) node;

    int ret = 0;

    mutex_acquire(&socket->mutex);

    if (socket->shutdown & SHUT_WR) {
        mutex_release(&socket->mutex);
        return 0;
    }

    ipv4_address_t dest_addr;

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
    }

    ipv4_protocol_t protocol = socket->protocol;
    struct netif* bound_netif = socket->bound_netif;

    mutex_release(&socket->mutex);

    void* datagram = kmalloc(count);
    if (unlikely(!datagram)) {
        return -ENOMEM;
    }

    if ((ret = USER_MEMCPY_MAYBE_FROM_USER(datagram, buf, count)) < 0) {
        kfree(datagram);
        return ret;
    }

    ret = ipv4_send(datagram, count, dest_addr, protocol, bound_netif);

    kfree(datagram);

    if (ret < 0) {
        return ret;
    }

    return count;
}

static ssize_t raw_getsockname(struct socket_node* node, struct sockaddr* addr, socklen_t addr_len) {
    struct raw_socket* socket = (struct raw_socket*) node;

    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    mutex_acquire(&socket->mutex);

    struct sockaddr_in* sin = (struct sockaddr_in*) addr;
    memset(sin, 0, sizeof(struct sockaddr_in));

    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(socket->local_addr);

    mutex_release(&socket->mutex);

    return sizeof(struct sockaddr_in);
}

static ssize_t raw_getpeername(struct socket_node* node, struct sockaddr* addr, socklen_t addr_len) {
    struct raw_socket* socket = (struct raw_socket*) node;

    if (addr_len < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    mutex_acquire(&socket->mutex);

    if (!socket->connected) {
        mutex_release(&socket->mutex);
        return -ENOTCONN;
    }

    struct sockaddr_in* sin = (struct sockaddr_in*) addr;
    memset(sin, 0, sizeof(struct sockaddr_in));

    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(socket->remote_addr);

    mutex_release(&socket->mutex);

    return sizeof(struct sockaddr_in);
}

static int raw_shutdown(struct socket_node* node, int how) {
    if (how & ~SHUT_RDWR) {
        return -EINVAL;
    }

    mutex_acquire(&node->mutex);

    node->shutdown |= how;

    mutex_release(&node->mutex);
    return 0;
}

static short raw_poll(struct socket_node* node, short events, struct poll_table* pt) {
    struct raw_socket* socket = (struct raw_socket*) node;

    mutex_acquire(&socket->mutex);

    short revents = 0;

    if (events & POLLIN && !(socket->shutdown & SHUT_RD)) {
        if (socket->rx_head) {
            revents |= POLLIN;
        } else {
            poll_table_add(pt, &socket->rx_wq);
        }
    }

    if (events & POLLOUT && !(socket->shutdown & SHUT_WR)) {
        revents |= POLLOUT;
    }

    mutex_release(&socket->mutex);

    return revents;
}

static void raw_destroy(struct socket_node* node) {
    struct raw_socket* socket = (struct raw_socket*) node;

    bool int_state = spinlock_acquire_irqsave(&raw_socket_list_lock);
    DLIST_REMOVE(raw_socket_list, socket, prev, next);
    spinlock_release_irqsave(&raw_socket_list_lock, int_state);

    mutex_acquire(&socket->mutex);

    struct receive_data* data = socket->rx_head;
    while (data) {
        struct receive_data* next = data->next;

        kfree(data->buf);
        kfree(data);

        data = next;
    }

    kfree(socket);
}

static void process(struct raw_socket* socket, const struct ipv4_header* header, ipv4_address_t source, ipv4_address_t destination) {
    mutex_acquire(&socket->mutex);

    if ((socket->shutdown & SHUT_RD) || (socket->protocol != header->protocol)) {
        mutex_release(&socket->mutex);
        return;
    }

    if ((socket->bound && socket->local_addr != destination) || (socket->connected && socket->remote_addr != source)) {
        mutex_release(&socket->mutex);
        return;
    }

    mutex_release(&socket->mutex);

    struct receive_data* data = kmalloc(sizeof(struct receive_data));
    if (unlikely(!data)) {
        return;
    }

    data->addr = htonl(source);
    data->len = ntohs(header->length);
    data->next = NULL;

    data->buf = kmalloc(data->len);
    if (unlikely(!data->buf)) {
        kfree(data);
        return;
    }

    memcpy(data->buf, header, data->len);

    mutex_acquire(&socket->mutex);

    if (socket->rx_tail) {
        socket->rx_tail->next = data;
    } else {
        socket->rx_head = data;
    }

    socket->rx_tail = data;

    mutex_release(&socket->mutex);

    wait_queue_wake_all(&socket->rx_wq);
}

void raw_socket_receive(const struct ipv4_header* header, ipv4_address_t source, ipv4_address_t destination) {
    bool int_state = spinlock_acquire_irqsave(&raw_socket_list_lock);

    struct raw_socket* socket = raw_socket_list;
    while (socket) {
        struct raw_socket* next = socket->next;

        VFS_NODE_REF(socket);
        if (next) {
            VFS_NODE_REF(next);
        }

        spinlock_release_irqsave(&raw_socket_list_lock, int_state);

        process(socket, header, source, destination);

        VFS_NODE_UNREF(socket);

        int_state = spinlock_acquire_irqsave(&raw_socket_list_lock);

        socket = next;
        if (socket) {
            VFS_NODE_UNREF(socket);
        }
    }

    spinlock_release_irqsave(&raw_socket_list_lock, int_state);
}

int raw_socket_create(ipv4_protocol_t protocol, struct socket_node** ret) {
    struct raw_socket* socket = kmallocz(sizeof(struct raw_socket));
    if (unlikely(!socket)) {
        return -ENOMEM;
    }

    socket->sockops = &raw_sockops;
    socket->protocol = protocol;

    wait_queue_init(&socket->rx_wq);

    mutex_init(&socket->mutex);

    bool int_state = spinlock_acquire_irqsave(&raw_socket_list_lock);
    DLIST_PUSH_FRONT(raw_socket_list, socket, prev, next);
    spinlock_release_irqsave(&raw_socket_list_lock, int_state);

    *ret = (struct socket_node*) socket;
    return 0;
}

void raw_socket_init(void) {
    spinlock_init(&raw_socket_list_lock);
}
