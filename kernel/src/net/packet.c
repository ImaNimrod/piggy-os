#include <mem/slab.h>
#include <net/eth.h>
#include <net/packet.h>
#include <utils/macros.h>
#include <utils/panic.h>

static struct slab_cache* packet_cache = NULL;

struct packet* packet_alloc(struct netif* netif, uint16_t length) {
    struct packet* packet = slab_cache_alloc(packet_cache);
    if (unlikely(packet == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for packet");
    }

    packet->netif = netif;
    packet->length = length;

    packet->buf = kmalloc(packet->length);

    if (unlikely(packet->buf == NULL)) {
        kpanic(NULL, true, "failed to allocate memory for packet");
    }

    return packet;
}

void packet_free(struct packet* packet) {
    kfree(packet->buf);
    slab_cache_free(packet_cache, (void*) packet);
}

void packet_init(void) {
    packet_cache = slab_cache_create("struct packet cache", sizeof(struct packet));
    if (unlikely(packet_cache == NULL)) {
        kpanic(NULL, false, "failed to initialize object cache for packet structs");
    }
}
