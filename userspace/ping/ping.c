#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/ip.h>

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t data[];
} __attribute__((packed));

static volatile sig_atomic_t stop = 0;

static uint16_t checksum(void* buf, size_t len) {
    uint32_t sum = 0;
    uint16_t* p = buf;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }

    if (len) {
        sum += *(uint8_t*) p;
    }

    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;
}

static void sigint_handler(int signum) {
    (void) signum;
    stop = 1;
}

static void usage(void) {
    fprintf(stderr, "usage: ping [-c COUNT] HOST\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    bool audible = false;
    const char* interface = NULL;
    unsigned long ping_count = 0;

    char* end_ptr;

    int c;
    while ((c = getopt(argc, argv, "ac:I:")) != -1) {
        switch (c) {
            case 'a':
                audible = true;
                break;
            case 'c':
                errno = 0;

                ping_count = strtoul(optarg, &end_ptr, 10);
                if (errno != 0 || ping_count == 0 || optarg == end_ptr || *end_ptr) {
                    warnx("invalid count: '%s'", optarg);
                    usage();
                }
                break;
            case 'I':
                interface = optarg;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        warnx("missing operand");
        usage();
    }

    if (argc > 1) {
        warnx("extra operands provided");
        usage();
    }

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        err(EXIT_FAILURE, "socket");
    }

    if (interface) {
        struct sockaddr_in sin = {
            .sin_family = AF_INET,
        };

        if (inet_pton(AF_INET, interface, &sin.sin_addr) == 1) {
            if (bind(fd, (struct sockaddr*) &sin, sizeof(sin)) < 0) {
                err(EXIT_FAILURE, "bind");
            }
        } else {
            struct ifreq ifr = {};
            strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';

            if (ioctl(fd, SIOCIFBIND, &ifr) < 0) {
                err(EXIT_FAILURE, "ioctl(SIOCIFBIND)");
            }
        }
    }

    const char* hostname = argv[0];

    struct addrinfo hints = {
        .ai_family = AF_INET,
    };

    struct addrinfo* result;

    int error = getaddrinfo(hostname, NULL, &hints, &result);
    if (error) {
        errx(EXIT_FAILURE, "failed to host '%s': %s", hostname, gai_strerror(error));
    }

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        err(EXIT_FAILURE, "signal(SIGINT)");
    }

    srand(time(NULL) ^ getpid());

    uint16_t identifier = rand();
    uint16_t sequence = 1;

    size_t num_rx = 0;
    size_t num_tx = 0;

    uint8_t packet[1024];

    char ip[INET_ADDRSTRLEN];

    printf("ping %s (%s) 33 bytes of data\n",
            hostname, inet_ntop(AF_INET, &((struct sockaddr_in*) result->ai_addr)->sin_addr, ip, sizeof(ip)));

    while (!stop) {
        struct icmp_header* header = (void*) packet;
        header->type = ICMP_ECHO_REQUEST;
        header->code = 0;
        header->identifier = htons(identifier);
        header->sequence = htons(sequence++);

        size_t packet_len = sizeof(struct icmp_header) + 5;

        header->checksum = 0;
        header->checksum = checksum(packet, packet_len);

        struct timespec start;
        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            err(EXIT_FAILURE, "clock_gettime(CLOCK_MONOTONIC)");
        }

        ssize_t ret = sendto(fd, packet, packet_len, 0, (struct sockaddr*) result->ai_addr, result->ai_addrlen);
        if (ret < 0) {
            err(EXIT_FAILURE, "sendto");
        }

        num_tx++;

        for (;;) {
            ssize_t nrecv = recv(fd, packet, sizeof(packet), 0);

            struct timespec end;
            if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
                err(EXIT_FAILURE, "clock_gettime(CLOCK_MONOTONIC)");
            }

            if (nrecv < 0){
                err(EXIT_FAILURE, "recv");
            } else if (nrecv < (ssize_t) (sizeof(struct iphdr) + sizeof(struct icmp_header))) {
                continue;
            }

            struct iphdr* iphdr = (void*) packet;
            struct icmp_header* reply = (void*) (packet + (iphdr->ihl * 4));

            if (reply->type == ICMP_ECHO_REPLY && ntohs(reply->identifier) == identifier) {
                num_rx++;

                if (audible) {
                    putchar('\a');
                }

                double ms = ((uint64_t) (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec)) / 1000000.0;
                printf("%zd bytes from %s: icmp_seq=%u time=%.3f ms\n", nrecv, ip, ntohs(reply->sequence), ms);
                break;
            }
        }

        if (ping_count != 0 && (num_tx >= ping_count)) {
            break;
        }

        sleep(1);
    }

    printf("\n--- %s ping statistics ---\n", hostname);
    printf("%zu packets transmitted, %zu packets received ",  num_tx, num_rx);

    size_t packet_loss = 0;
    if (num_tx != 0) {
        packet_loss = (num_tx - num_rx) * 100 / num_tx;
    }

    printf("%zu%% packet loss\n", packet_loss);

    close(fd);

    return EXIT_SUCCESS;
}
