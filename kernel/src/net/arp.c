#include <mem/slab.h>
#include <net/arp.h>
#include <net/netif.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/mutex.h>
#include <utils/string.h>
#include <utils/wait_queue.h>

#define ARP_HWTYPE_ETH      0x0001
#define ARP_PROTYPE_IPV4    0x0800

#define ARP_OPCODE_REQUEST  1
#define ARP_OPCODE_REPLY    2

struct arp_header {
    uint16_t hwtype;
    uint16_t protype;
    uint8_t hwsize;
    uint8_t prosize;
    uint16_t opcode;
    uint8_t data[];
} __attribute__((packed));

struct arp_data_ipv4 {
    mac_address_t smac;
    ipv4_address_t sip;
    mac_address_t dmac;
    ipv4_address_t dip;
} __attribute__((packed));

struct arp_cache_entry {
    ipv4_address_t ip;
    mac_address_t mac;
};

static hashmap_t* arp_cache;
static mutex_t arp_cache_mutex;
static struct wait_queue arp_cache_wq;

static void send_reply(struct netif* netif, mac_address_t* dmac, ipv4_address_t dip) {
    struct packet packet;
    if (!netif->alloc_packet(netif, &packet, sizeof(struct arp_header) + sizeof(struct arp_data_ipv4))) {
        return;
    }

    struct arp_header* header = (void*) ((uintptr_t) packet.buf + packet.offset);
    header->hwtype = htons(ARP_HWTYPE_ETH);
    header->protype = htons(ARP_PROTYPE_IPV4);
    header->hwsize = sizeof(mac_address_t);
    header->prosize = sizeof(ipv4_address_t);
    header->opcode = htons(ARP_OPCODE_REPLY);

    struct arp_data_ipv4* data = (void*) &header->data;
    memcpy(data->smac, netif->mac, sizeof(mac_address_t));
    data->sip = htonl(netif->ipv4_address);
    memcpy(data->dmac, dmac, sizeof(mac_address_t));
    data->dip = htonl(dip);

    netif->send_packet(netif, &packet, dmac, ETHERTYPE_ARP);
    netif->free_packet(netif, &packet);
}

static bool send_request(struct netif* netif, ipv4_address_t ip) {
    struct packet packet;
    if (!netif->alloc_packet(netif, &packet, sizeof(struct arp_header) + sizeof(struct arp_data_ipv4))) {
        return false;
    }

    struct arp_header* header = (void*) ((uintptr_t) packet.buf + packet.offset);
    header->hwtype = htons(ARP_HWTYPE_ETH);
    header->protype = htons(ARP_PROTYPE_IPV4);
    header->hwsize = sizeof(mac_address_t);
    header->prosize = sizeof(ipv4_address_t);
    header->opcode = htons(ARP_OPCODE_REQUEST);

    struct arp_data_ipv4* data = (void*) &header->data;
    memcpy(data->smac, netif->mac, sizeof(mac_address_t));
    data->sip = htonl(netif->ipv4_address);
    memset(data->dmac, 0, sizeof(mac_address_t));
    data->dip = htonl(ip);

    bool ret = netif->send_packet(netif, &packet, &BROADCAST_MAC, ETHERTYPE_ARP);

    netif->free_packet(netif, &packet);

    return ret;
}

void arp_handle(struct netif* netif, const void* buf) {
    const struct arp_header* header = buf;

    if (unlikely(ntohs(header->hwtype) != ARP_HWTYPE_ETH)) {
        return;
    }
    if (unlikely(ntohs(header->protype) != ARP_PROTYPE_IPV4)) {
        return;
    }

    struct arp_data_ipv4* data = (void*) &header->data;

    uint16_t opcode = ntohs(header->opcode);
    if (opcode == ARP_OPCODE_REQUEST) {
        if (ntohl(data->dip) == netif->ipv4_address) {
            send_reply(netif, &data->smac, ntohl(data->sip));
        }
    } else if (opcode == ARP_OPCODE_REPLY) {
        if (!memcmp(data->dmac, netif->mac, sizeof(mac_address_t))) {
            mutex_acquire(&arp_cache_mutex);

            void* mac = kmalloc(sizeof(mac_address_t));
            memcpy(mac, data->smac, sizeof(mac_address_t));

            ipv4_address_t ip = ntohl(data->sip);

            hashmap_set(arp_cache, &ip, sizeof(ip), mac);

            mutex_release(&arp_cache_mutex);

            wait_queue_wake_all(&arp_cache_wq);
        }
    }
}

bool arp_lookup(struct netif* netif, ipv4_address_t ip, mac_address_t* mac) {
    if (ip == BROADCAST_IPV4) {
        memcpy(mac, &BROADCAST_MAC, sizeof(mac_address_t));
        return true;
    }

    bool ret = false;

    mutex_acquire(&arp_cache_mutex);

    mac_address_t *cached;

    if ((ret = hashmap_get(arp_cache, &ip, sizeof(ip), (void**) &cached))) {
        memcpy(mac, cached, sizeof(mac_address_t));
        goto end;
    }

    mutex_release(&arp_cache_mutex);

    if (!(ret = send_request(netif, ip))) {
        goto end;
    }

    wait_queue_wait(&arp_cache_wq);

    for (;;) {
        mutex_acquire(&arp_cache_mutex);

        if (hashmap_get(arp_cache, &ip, sizeof(ip), (void **)&cached)) {
            memcpy(mac, cached, sizeof(mac_address_t));
            ret = true;
            break;
        }

        mutex_release(&arp_cache_mutex);

        wait_queue_wait(&arp_cache_wq);
    }

end:
    mutex_release(&arp_cache_mutex);
    return ret;
}

void arp_init(void) {
    arp_cache = hashmap_create(128);
    if (unlikely(!arp_cache)) {
        kpanic(NULL, false, "failed to initialize ARP cache");
    }

    mutex_init(&arp_cache_mutex);
    wait_queue_init(&arp_cache_wq);
}
