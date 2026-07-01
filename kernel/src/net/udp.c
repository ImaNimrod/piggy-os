#include <net/udp.h>
#include <utils/macros.h> 

struct udp_header {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

void udp_handle(struct netif* netif, ipv4_address_t source, const void* buf, size_t len) {
    if (unlikely(len < sizeof(struct udp_header))) {
        return;
    }

    struct udp_header* header = (void*) buf;

    // TODO: verify UDP packet checksums
}
