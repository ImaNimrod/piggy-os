#include <net/arp.h>
#include <net/ipv4.h>
#include <net/netif.h>
#include <net/raw.h>
#include <net/udp.h>
#include <printf.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/string.h>

char hostname[HOST_NAME_MAX] = "piggy";

int netif_count;
struct netif* netif_list;
spinlock_t netif_list_lock;

static uint32_t lo_count;
static uint32_t eth_count;

struct netif* netif_find(const char* name) {
    spinlock_acquire(&netif_list_lock);

    for (struct netif* n = netif_list; n; n = n->next) {
        if (strcmp(n->name, name) == 0) {
            spinlock_release(&netif_list_lock);
            return n;
        }
    }

    spinlock_release(&netif_list_lock);
    return NULL;
}

void netif_register(struct netif* netif) {
    spinlock_acquire(&netif_list_lock);

    switch (netif->type) {
        case NETIF_TYPE_LO:
            snprintf(netif->name, sizeof(netif->name), "lo%u", lo_count++);
            break;
        case NETIF_TYPE_ETH:
            snprintf(netif->name, sizeof(netif->name), "eth%u", eth_count++);
            break;
    }

    netif->index = netif_count++;

    SLIST_PUSH_FRONT(netif_list, netif, next);

    spinlock_release(&netif_list_lock);
}

void net_init(void) {
    spinlock_init(&netif_list_lock);

    arp_init();
    ipv4_init();

    raw_socket_init();
    udp_init();

    klog("[net] initialized networking subsystem\n");
}
