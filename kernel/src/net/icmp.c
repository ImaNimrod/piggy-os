#include <net/icmp.h> 
#include <utils/macros.h> 

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint8_t data[];
} __attribute__((packed));


void icmp_handle(ipv4_address_t source, const void* buf, uint16_t len) {
    if (unlikely(len < sizeof(struct icmp_header))) {
        return;
    }

    struct icmp_header* header = (void*) buf;

    if (inet_checksum(header, len) != 0) {
        return;
    }

    if (header->type == ICMP_TYPE_ECHO_REQUEST && header->code == 0) {
        header->type = ICMP_TYPE_ECHO_REPLY;
        header->checksum = 0;

        header->checksum = inet_checksum(header, len);

        ipv4_send(header, len, source, IPV4_PROTOCOL_ICMP, NULL);
    }
}
