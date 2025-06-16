#include <net/arp.h>
#include <net/eth.h>
#include <net/ipv4.h>
#include <utils/log.h>
#include <utils/macros.h>

#define IPV4_VERSION 0x04

uint16_t inet_checksum(void* data, uint16_t length) {
    register uint32_t sum = 0;

    uint16_t* ptr = (uint16_t*) data;
    for (uint16_t i = length; i >= 2; i -= 2) {
        sum += *ptr++;
    }

    sum = (sum & 0xffff) + (sum >> 16);
    if (sum > 0xffff) {
        sum += 1;
    }

    return ~sum;
}

void ipv4_handle(struct packet* packet, void* l3_data) {
    struct ipv4_header* header = l3_data;

    if (unlikely(header->version != IPV4_VERSION)) {
        return;
    }
    if (unlikely(header->ihl < 5)) {
        return;
    }
    if (header->ttl == 0) {
        // TODO: send ICMP error
        return;
    }

    uint16_t checksum = header->checksum;
    header->checksum = 0;
    if (inet_checksum(header, header->ihl * sizeof(uint32_t)) != checksum) {
        return;
    }

    // TODO: handle fragmentation
    klog("received IPV4 packet from " IPV4_FORMAT " | type: ", IPV4_PRINT(ntohl(header->source)));

    switch (header->protocol) {
        case IPV4_PROTOCOL_ICMP:
            klog("ICMP packet\n");
            break;
        case IPV4_PROTOCOL_TCP:
            klog("TCP packet\n");
            break;
        case IPV4_PROTOCOL_UDP:
            klog("IPV4 UDP packet\n");
            break;
    }
}
