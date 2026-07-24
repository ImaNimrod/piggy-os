#include <net/icmp.h> 
#include <utils/macros.h> 

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

struct icmp_echo_request {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t data[];
} __attribute__((packed));

void icmp_handle(ipv4_address_t source, const void* buf, uint16_t len) {
    if (unlikely(len < sizeof(struct icmp_echo_request))) {
        return;
    }

    struct icmp_echo_request* echo = (void*) buf;

    if (inet_checksum(echo, len) != 0) {
        return;
    }

    if (echo->type != ICMP_TYPE_ECHO_REQUEST || echo->code != 0) {
        return;
    }

    echo->type = ICMP_TYPE_ECHO_REPLY;
    echo->checksum = 0;
    echo->checksum = inet_checksum(echo, len);

    ipv4_send(echo, len, source, IPV4_PROTOCOL_ICMP, NULL);
}
