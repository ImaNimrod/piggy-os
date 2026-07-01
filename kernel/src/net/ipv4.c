#include <errno.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/netif.h>
#include <net/udp.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

#define IPV4_VERSION 0x04
#define IPV4_MAX_LEN 65535

uint16_t inet_checksum(void* buf, uint16_t len) {
    uint16_t* ptr = buf;
    uint32_t sum = 0;

    while (len >= 2) {
        sum += *ptr++;
        len -= 2;
    }

    if (len != 0) {
        sum += *(const uint8_t*) ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

void ipv4_handle(struct netif* netif, const void* buf) {
    struct ipv4_header* header = (void*) buf;

    if (unlikely(header->ihl < 5)) {
        return;
    }
    if (unlikely(header->version != IPV4_VERSION)) {
        return;
    }

    if (header->ttl == 0) {
        // TODO: send ICMP error
        return;
    }

    uint16_t header_size = header->ihl * sizeof(uint32_t);
    if (inet_checksum(header, header_size) != 0) {
        return;
    }

    ipv4_address_t source = ntohl(header->source);
    ipv4_address_t destination = ntohl(header->destination);

    if (destination != BROADCAST_IPV4 && destination != netif->ipv4_address) {
        return;
    }

    uint8_t* payload = (uint8_t*) header + header_size;
    size_t payload_len = ntohs(header->length) - header_size;

    switch (header->protocol) {
        case IPV4_PROTOCOL_ICMP:
            icmp_handle(netif, source, payload, payload_len);
            break;
        case IPV4_PROTOCOL_TCP:
            klog("TCP packet received... in your fucking dreams kiddo\n");
            break;
        case IPV4_PROTOCOL_UDP:
            udp_handle(netif, source, payload, payload_len);
            break;
    }
}

int ipv4_send(const void* buf, size_t len, ipv4_address_t destination, ipv4_protocol_t protocol, struct netif* netif) {
    size_t total_len = sizeof(struct ipv4_header) + len;
    if (total_len > IPV4_MAX_LEN) {
        return -EMSGSIZE;
    }

    mac_address_t mac;
    if (!arp_lookup(netif, destination, &mac)) {
        return -ENETUNREACH;
    }

    struct ipv4_header header = {
        .ihl = 5,
        .version = IPV4_VERSION,
        .tos = 1,
        .length = htons((uint16_t) total_len),
        .identification = 0,
        .flags = 0,
        .fragment_offset = 0,
        .ttl = 64,
        .protocol = protocol,
        .checksum = 0,
        .source = htonl(netif->ipv4_address),
        .destination = htonl(destination),
    };

    header.checksum = inet_checksum(&header, sizeof(header));

    struct packet packet;
    if (!netif->alloc_packet(netif, &packet, total_len)) {
        return -ENOMEM;
    }

    uint8_t* p = (uint8_t*) packet.buf + packet.offset;
    memcpy(p, &header, sizeof(header));
    memcpy(p + sizeof(header), buf, len);

    bool ret = netif->send_packet(netif, &packet, &mac, ETHERTYPE_IPV4);

    netif->free_packet(netif, &packet);

    return ret ? 0 : -EIO;
}
