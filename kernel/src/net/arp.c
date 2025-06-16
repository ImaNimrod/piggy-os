#include <mem/slab.h>
#include <net/arp.h>
#include <net/eth.h>
#include <stdint.h>
#include <sys/scheduler.h>
#include <utils/hashmap.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/panic.h>
#include <utils/spinlock.h>
#include <utils/string.h>

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

static hashmap_t* arp_cache = NULL;
static spinlock_t arp_cache_lock = {0};

static bool ipv4_key_compare(const void* key1, const void* key2) {
    return (uintptr_t) key1 == (uintptr_t) key2;
}

static void* ipv4_key_dupe(const void* key) {
    return (void*) key;
}

static void ipv4_key_free(void* key) {
    (void) key;
}

static size_t ipv4_key_hash(const void* key) {
    uint32_t hash = (uintptr_t) key;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    return hash;
}

static void mac_value_free(void* value) {
    kfree(value);
}

static void arp_reply(struct netif* netif, mac_address_t* dmac, ipv4_address_t dip) {
    struct packet* packet = packet_alloc(netif, sizeof(struct eth_header) + sizeof(struct arp_header) + sizeof(struct arp_data_ipv4));

    struct arp_header* header = eth_populate_packet(packet, dmac, ETHERTYPE_ARP);
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

    netif_send_packet(netif, packet);
}

void arp_handle(struct packet* packet, void* l3_data) {
    struct arp_header* header = l3_data;

    if (unlikely(ntohs(header->hwtype) != ARP_HWTYPE_ETH)) {
        return;
    }
    if (unlikely(ntohs(header->protype) != ARP_PROTYPE_IPV4)) {
        return;
    }

    struct arp_data_ipv4* data = (void*) &header->data;

    uint16_t opcode = ntohs(header->opcode);
    if (opcode == ARP_OPCODE_REQUEST) {
        if (ntohl(data->dip) == packet->netif->ipv4_address) {
            arp_reply(packet->netif, &data->smac, ntohl(data->sip));
        }
    } else if (opcode == ARP_OPCODE_REPLY) {
        if (!memcmp(data->dmac, packet->netif->mac, sizeof(mac_address_t))) {
            spinlock_acquire(&arp_cache_lock);

            void* mac = kmalloc(sizeof(mac_address_t));
            memcpy(mac, data->smac, sizeof(mac_address_t));
            hashmap_set(arp_cache, (const void*) (uintptr_t) ntohl(data->sip), mac);

            spinlock_release(&arp_cache_lock);
        }
    }
}

bool arp_lookup_ip(struct netif* netif, ipv4_address_t ip, mac_address_t* mac) {
    if (ip == 0xffffffff) {
        memcpy(mac, &broadcast_mac, sizeof(mac_address_t));
        return true;
    }

    spinlock_acquire(&arp_cache_lock);

    mac_address_t* cache_mac;

    bool ret = hashmap_get(arp_cache, (const void*) (uintptr_t) ip, (void**) &cache_mac);
    if (ret) {
        memcpy(mac, cache_mac, sizeof(mac_address_t));
    }

    spinlock_release(&arp_cache_lock);

    return ret;
}

void arp_init(void) {
    arp_cache = hashmap_create(20, ipv4_key_compare, ipv4_key_dupe, ipv4_key_free, ipv4_key_hash, mac_value_free);
    if (unlikely(arp_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize ARP cache");
    }
}
