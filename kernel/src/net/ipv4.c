#include <dev/net/loopback.h>
#include <errno.h>
#include <mem/slab.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/netif.h>
#include <net/raw.h>
#include <net/udp.h>
#include <utils/list.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/string.h>

#define IPV4_FLAG_MF (1 << 2)

#define IPV4_VERSION 0x04
#define IPV4_MAX_LEN 65535

struct routing_table_entry {
    struct netif* netif;
    ipv4_address_t addr;
    ipv4_address_t gateway;
    ipv4_address_t mask;
    struct routing_table_entry* next;
};

static struct routing_table_entry* routing_table;
static mutex_t routing_table_mutex;

static inline uint32_t mask_to_prefix(ipv4_address_t mask) {
    uint32_t prefix = 0;

    while (mask & 0x80000000) {
        prefix++;
        mask <<= 1;
    }

    return prefix;
}

static struct routing_table_entry* get_route(ipv4_address_t ip) {
    struct routing_table_entry* best = NULL;
    uint32_t best_prefix = 0;

    mutex_acquire(&routing_table_mutex);

    struct routing_table_entry* entry;
    SLIST_FOREACH(routing_table, entry, next) {
        if ((ip & entry->mask) == (entry->addr & entry->mask)) {
            uint32_t prefix = mask_to_prefix(entry->mask);
            if (!best || prefix > best_prefix) {
                best = entry;
                best_prefix = prefix;
            }
        }
    }

    mutex_release(&routing_table_mutex);
    return best;
}

uint16_t inet_checksum(const void* restrict buf, uint16_t len) {
    const uint16_t* ptr = buf;

    uint64_t sum = 0;

    while (len >= 2) {
        sum += *ptr++;
        len -= 2;
    }

    if (len) {
        sum += *(const uint8_t *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;
}

int ipv4_route_add(struct netif* netif, ipv4_address_t addr, ipv4_address_t gateway, ipv4_address_t mask) {
    struct routing_table_entry* entry = kmalloc(sizeof(struct routing_table_entry));
    if (unlikely(!entry)) {
        return -ENOMEM;
    }
    entry->netif = netif;
    entry->addr = addr;
    entry->gateway = gateway;
    entry->mask = mask;

    mutex_acquire(&routing_table_mutex);
    SLIST_PUSH_FRONT(routing_table, entry, next);
    mutex_release(&routing_table_mutex);

    return 0;
}

int ipv4_route_delete(ipv4_address_t addr, ipv4_address_t gateway, ipv4_address_t mask) {
    int ret = -EINVAL;

    mutex_acquire(&routing_table_mutex);

    struct routing_table_entry* entry;
    SLIST_FOREACH(routing_table, entry, next) {
        if (addr == entry->addr && gateway == entry->gateway && mask == entry->mask) {
            SLIST_REMOVE(routing_table, entry, next);
            kfree(entry);
            ret = 0;
            goto end;
        }
    }

end:
    mutex_release(&routing_table_mutex);
    return ret;
}

void ipv4_handle(struct netif* netif, const void* buf) {
    struct ipv4_header* header = (void*) buf;

    if (unlikely(header->ihl < 5)) {
        return;
    }
    if (unlikely(header->version != IPV4_VERSION)) {
        return;
    }

    if ((header->flags & IPV4_FLAG_MF) || (header->fragment_offset != 0)) {
        return;
    }

    if ((header->ttl - 1) == 0) {
        return;
    }

    uint16_t header_size = header->ihl * sizeof(uint32_t);
    if (inet_checksum(header, header_size) != 0) {
        return;
    }

    ipv4_address_t source = ntohl(header->source);
    ipv4_address_t destination = ntohl(header->destination);

    if (destination != BROADCAST_IPV4 && destination != netif->ipv4_addr) {
        return;
    }

    const void* payload = (uint8_t*) header + header_size;
    uint16_t payload_len = ntohs(header->length) - header_size;

    raw_socket_receive(header, source, destination);

    switch (header->protocol) {
        case IPV4_PROTOCOL_ICMP:
            icmp_handle(source, payload, payload_len);
            break;
        case IPV4_PROTOCOL_TCP:
            klog("TCP packet received... in your fucking dreams kiddo\n");
            break;
        case IPV4_PROTOCOL_UDP:
            udp_handle(source, destination, payload, payload_len);
            break;
    }
}

int ipv4_send(const void* buf, size_t len, ipv4_address_t destination, ipv4_protocol_t protocol, struct netif* broadcast_netif) {
    size_t total_len = sizeof(struct ipv4_header) + len;
    if (total_len > IPV4_MAX_LEN) {
        return -EMSGSIZE;
    }

    mac_address_t mac;
    struct netif* netif;

    if (destination != BROADCAST_IPV4) {
        struct routing_table_entry* route = get_route(destination);
        if (!route) {
            return -ENETUNREACH;
        }

        if (!arp_lookup(route->netif, route->gateway ? route->gateway : destination, &mac)) {
            return -ENETUNREACH;
        }

        netif = route->netif;
    } else {
        memcpy(mac, &BROADCAST_MAC, sizeof(mac));
        netif = broadcast_netif;
    }

    size_t fragment_payload = netif->mtu - sizeof(struct ipv4_header);
    fragment_payload &= ~7ULL;

    size_t fragment_count = DIV_CEIL(len, fragment_payload);

    for (size_t i = 0; i < fragment_count; i++) {
        size_t offset = i * fragment_payload;
        size_t fragment_len = MIN(fragment_payload, len - offset);

        struct packet packet;
        if (!netif->alloc_packet(netif, &packet, sizeof(struct ipv4_header) + fragment_len)) {
            return -ENOMEM;
        }

        struct ipv4_header header = {
            .ihl = 5,
            .version = IPV4_VERSION,
            .tos = 0,
            .length = htons((uint16_t) sizeof(struct ipv4_header) + fragment_len),
            .identification = htons(atomic_fetch_add_explicit(&netif->ip_id_counter, 1, memory_order_relaxed)),
            .flags = (i == fragment_count - 1) ? 0 : IPV4_FLAG_MF,
            .fragment_offset = htons((uint16_t) ((i * fragment_payload) >> 3)),
            .ttl = 64,
            .protocol = protocol,
            .checksum = 0,
            .source = htonl(netif->ipv4_addr),
            .destination = htonl(destination),
        };

        header.checksum = inet_checksum(&header, sizeof(header));

        uint8_t* p = (uint8_t*) packet.buf + packet.offset;
        memcpy(p, &header, sizeof(header));
        memcpy(p + sizeof(header), (const void*) ((uintptr_t) buf + (i * fragment_payload)), fragment_len);

        bool ret = netif->send_packet(netif, &packet, &mac, ETHERTYPE_IPV4);

        netif->free_packet(netif, &packet);

        if (!ret) {
            return -EIO;
        }
    }

    return 0;
}

void ipv4_init(void) {
    mutex_init(&routing_table_mutex);

    if (ipv4_route_add(loopback_init(), IPV4_ADDRESS(127, 0, 0, 1), IPV4_ADDRESS(0, 0, 0, 0), IPV4_ADDRESS(255, 0, 0, 0)) < 0) {
        kpanic(NULL, false, "failed to add routing table entry for loopback device");
    }
}
