#include <dev/net/loopback.h>
#include <mem/slab.h>
#include <net/arp.h>
#include <net/eth.h>
#include <net/netif.h>
#include <sys/process.h>
#include <sys/scheduler.h>
#include <utils/log.h>
#include <utils/macros.h>
#include <utils/string.h>

static void netif_packet_handler(struct netif* netif) {
    for (;;) {
        struct packet* packet = NULL;
        if (!vector_pop(netif->packet_queue, &packet)) {
            continue;
        }

        eth_handle(packet);

        packet_free(packet);
    }
}

struct netif* netif_create(void) {
    struct netif* netif = kmalloc(sizeof(struct netif));
    if (unlikely(netif == NULL)) {
        kpanic(NULL, false, "failed to allocate memory for network interface");
    }

    netif->packet_queue = vector_create(sizeof(struct packet*));
    if (unlikely(netif->packet_queue == NULL)) {
        kpanic(NULL, false, "failed to create packet queue for network interface");
    }

    struct thread* netif_packet_handler_thread = thread_create_kernel((uintptr_t) netif_packet_handler, netif);
    scheduler_enqueue(netif_packet_handler_thread);

    return netif;
}

bool netif_add_packet(struct netif* netif, const void* buf, uint16_t length) {
    struct packet* packet = packet_alloc(netif, length);
    memcpy(packet->buf, buf, length);

    spinlock_acquire(&netif->packet_queue_lock);
    bool ret = vector_push(netif->packet_queue, &packet);
    spinlock_release(&netif->packet_queue_lock);

    return ret;
}

bool netif_send_packet(struct netif* netif, struct packet* packet) {
    if (packet->length > netif->mtu) {
        return false;
    }

    spinlock_acquire(&netif->send_lock);
    bool ret = netif->send_packet(netif, packet);
    spinlock_release(&netif->send_lock);

    packet_free(packet);

    return ret;
}

void net_init(void) {
    klog("[net] initialized networking subsystem\n");

    packet_init();
    arp_init();

    loopback_init();
}
