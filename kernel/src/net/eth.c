#include <net/arp.h>
#include <net/eth.h>
#include <net/ipv4.h>
#include <net/netif.h>
#include <utils/macros.h>
#include <utils/string.h>

mac_address_t BROADCAST_MAC = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

void eth_handle(struct netif* netif, const void* buf) {
    const struct eth_header* header = buf;

    switch (ntohs(header->ethertype)) {
        case ETHERTYPE_ARP:
            arp_handle(netif, (void*) (header + 1));
            break;
        case ETHERTYPE_IPV4:
            ipv4_handle(netif, (void*) (header + 1));
            break;
    }
}

void eth_populate_packet(struct netif* netif, mac_address_t* destination, ethertype_t type, void* buf) {
    struct eth_header* header = buf;
    memcpy(header->smac, netif->mac, sizeof(mac_address_t));
    memcpy(header->dmac, destination, sizeof(mac_address_t));
    header->ethertype = htons(type);
}
