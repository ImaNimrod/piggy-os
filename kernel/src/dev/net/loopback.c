#include <dev/net/loopback.h>
#include <mem/paging.h>
#include <mem/pmm.h>
#include <mem/slab.h>
#include <net/eth.h>
#include <net/netif.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

static bool loopback_alloc_packet(struct netif* netif, struct packet* packet, size_t size) {
    if (size > netif->mtu) {
        return false;
    }

    size_t total_size = sizeof(struct eth_header) + size;

    packet->buf = (void*) (pmm_alloc(DIV_CEIL(total_size, PAGE_SIZE_4KB)) + HIGH_VMA);
    packet->size = total_size;
    packet->offset = sizeof(struct eth_header);
    return true;
}

static void loopback_free_packet(struct netif* netif, struct packet* packet) {
    (void) netif;

    pmm_free((uintptr_t) packet->buf - HIGH_VMA, DIV_CEIL(packet->size, PAGE_SIZE_4KB));
}

static bool loopback_send_packet(struct netif* netif, struct packet* packet, mac_address_t* destination, ethertype_t type) {
    eth_populate_packet(netif, destination, type, packet->buf);
    eth_handle(netif, packet->buf);
    return true;
}

static void loopback_update_flags(struct netif* netif, uint16_t old_flags) {
    (void) netif;
    (void) old_flags;
}

struct netif* loopback_init(void) {
    struct netif* netif = kmalloc(sizeof(struct netif));
    if (unlikely(!netif)) {
        kpanic(NULL, false, "failed to create loopback network interface");
    }
    netif->type = NETIF_TYPE_LO;
    netif->flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK;
    netif->mtu = 65535;
    netif->alloc_packet = loopback_alloc_packet;
    netif->free_packet = loopback_free_packet;
    netif->send_packet = loopback_send_packet;
    netif->update_flags = loopback_update_flags;

    memset(netif->mac, 0, sizeof(mac_address_t));
    netif->ipv4_address = IPV4_ADDRESS(127, 0, 0, 1);
    netif->ipv4_mask = IPV4_ADDRESS(255, 0, 0, 0);

    netif_register(netif);

    return netif;
}
