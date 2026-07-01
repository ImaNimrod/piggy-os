#include <dev/net/loopback.h>
#include <net/arp.h>
#include <net/netif.h>
#include <utils/log.h>

void net_init(void) {
    klog("[net] initialized networking subsystem\n");

    arp_init();
    loopback_init();
}
