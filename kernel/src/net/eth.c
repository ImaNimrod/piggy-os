#include <net/arp.h>
#include <net/eth.h>
#include <net/ipv4.h>
#include <net/netif.h>
#include <utils/macros.h>
#include <utils/string.h>

mac_address_t broadcast_mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

void eth_handle(struct packet* packet) {
    struct eth_header* header = packet->buf;

    switch (ntohs(header->ethertype)) {
        case ETHERTYPE_ARP:
            arp_handle(packet, (void*) (header + 1));
            break;
        case ETHERTYPE_IPV4:
            ipv4_handle(packet, (void*) (header + 1));
            break;
    }
}

void* eth_populate_packet(struct packet* packet, mac_address_t* dmac, uint16_t ethertype) {
    struct eth_header* header = packet->buf;

    memcpy(header->smac, packet->netif->mac, sizeof(mac_address_t));
    memcpy(header->dmac, dmac, sizeof(mac_address_t));

    header->ethertype = htons(ethertype);

    return header + 1;
}
