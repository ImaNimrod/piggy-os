#include <dev/net/loopback.h>
#include <mem/slab.h>
#include <net/netif.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

static bool loopback_send_packet(struct netif* netif, struct packet* packet, mac_address_t* destination, ethertype_t type) {
    (void) netif;
    (void) packet;
    (void) destination;
    (void) type;
    return true;
}

static void loopback_update_flags(struct netif* netif, uint16_t old_flags) {
    (void) old_flags;
    netif->flags |= NETIF_FLAG_RUNNING;
}

void loopback_init(void) {
    struct netif* netif = kmalloc(sizeof(struct netif));
    if (unlikely(!netif)) {
        kpanic(NULL, false, "failed to create loopback network interface");
    }
    netif->flags = NETIF_FLAG_RUNNING | NETIF_FLAG_LOOPBACK;
    netif->mtu = 65535;
    netif->send_packet = loopback_send_packet;
    netif->update_flags = loopback_update_flags;

    memset(netif->mac, 0, sizeof(mac_address_t));
    netif->ipv4_address = IPV4_ADDRESS(127, 0, 0, 1);
    netif->ipv4_gateway = IPV4_ADDRESS(0, 0, 0, 0);
    netif->ipv4_subnet_mask = IPV4_ADDRESS(255, 0, 0, 0);
}
