#define _POSIX_C_SOURCE 202405L

#include <arpa/inet.h>

#include <netinet/in.h>

#include <sys/socket.h>

#include <err.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> 

#include <config.h>

#define _PATH_CONFIG "/etc/ntp.conf"

#define NTP_EPOCH       2208988800ULL
#define NTP_INTERVAL    (11 * 60)

struct ntp_packet {
    uint8_t li_vn_mode;

    uint8_t stratum;
    uint8_t poll;
    int8_t precision;

    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t reference_id;

    uint64_t reference_timestamp;
    uint64_t originate_timestamp;
    uint64_t receive_timestamp;
    uint64_t transmit_timestamp;
} __attribute__((packed));

struct ntp_peer {
    struct sockaddr_storage addr;
    socklen_t addr_len;

    bool usable;

    uint8_t stratum;

    int64_t offset;
    uint64_t delay;

    uint64_t originate_timestamp;
    uint64_t request_time;

    struct ntp_peer* next;
};

static struct ntp_peer* peer_list;

static inline int64_t ntp_offset_to_ns(int64_t offset) {
    return (offset * 1000000000LL) >> 32;
}

static inline uint64_t unix_to_ntp(void) {
    struct timespec realtime;
    if (clock_gettime(CLOCK_REALTIME, &realtime) < 0) {
        err(EXIT_FAILURE, "clock_gettime(CLOCK_REALTIME)");
    }

    uint64_t sec = realtime.tv_sec + NTP_EPOCH;
    uint64_t frac = ((uint64_t) realtime.tv_nsec << 32) / 1000000000ULL;
    return (sec << 32) | frac;
}

static void load_servers(config_t* config, struct ntp_peer** peer_list) {
    config_iterator_t iter;

    config_iterator_init(&iter, &config->root, "server");

    config_node_t* node;
    while ((node = config_iterator_next(&iter))) {
        const char* hostname;
        if (config_value_get_string(node, 0, &hostname) < 0) {
            warnx("config: invalid server entry");
            continue;
        }

        struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_DGRAM,
            .ai_protocol = IPPROTO_UDP,
        };

        struct addrinfo* result;

        int error = getaddrinfo(hostname, "ntp", &hints, &result);
        if (error) {
            warnx("failed to resolve %s", hostname);
            continue;
        }

        for (struct addrinfo* ai = result; ai; ai = ai->ai_next) {
            struct ntp_peer* peer = calloc(1, sizeof(struct ntp_peer));
            if (!peer) {
                err(EXIT_SUCCESS, "calloc");
            }

            if (ai->ai_addrlen > sizeof(peer->addr)) {
                free(peer);
                continue;
            }

            memcpy(&peer->addr, ai->ai_addr, ai->ai_addrlen);
            peer->addr_len = ai->ai_addrlen;

            peer->usable = false;

            peer->next = *peer_list;
            *peer_list = peer;
        }

        freeaddrinfo(result);
    }
}

static bool receive_response(int sockfd) {
    struct ntp_packet pkt;

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    ssize_t n = recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr*) &addr, &addr_len);
    if (n < (ssize_t) sizeof(pkt)) {
        return false;
    }

    uint8_t mode = pkt.li_vn_mode & 0x07;
    uint8_t version = (pkt.li_vn_mode >> 3) & 0x07;

    if (mode != 4 || (version != 3 && version != 4)) {
        return false;
    }

    if (pkt.stratum == 0 || pkt.stratum > 15) {
        return false;
    }

    if (pkt.receive_timestamp == 0 || pkt.transmit_timestamp == 0) {
        return false;
    }

    uint64_t t4 = unix_to_ntp();

    uint64_t originate = __builtin_bswap64(pkt.originate_timestamp);

    struct ntp_peer* peer = peer_list;
    while (peer) {
        if (peer->originate_timestamp == originate) {
            break;
        }

        peer = peer->next;
    }

    if (!peer) {
        return false;
    }

    uint64_t t1 = peer->originate_timestamp;
    uint64_t t2 = __builtin_bswap64(pkt.receive_timestamp);
    uint64_t t3 = __builtin_bswap64(pkt.transmit_timestamp);

    peer->offset = ((int64_t) (t2 - t1) + (int64_t) (t3 - t4)) / 2;
    peer->delay = (t4 - t1) - (t3 - t2);

    peer->stratum = pkt.stratum;
    peer->usable = true;

    return true;
}

static struct ntp_peer* select_best_peer(void) {
    struct ntp_peer* best = NULL;

    for (struct ntp_peer* peer = peer_list; peer; peer = peer->next) {
        if (!peer->usable) {
            continue;
        }

        if (!best || peer->delay < best->delay) {
            best = peer;
        }
    }

    return best;
}

static bool send_request(int sockfd, struct ntp_peer* peer) {
    struct ntp_packet pkt = {};
    pkt.li_vn_mode = (0 << 6) | (4 << 3) | 3;

    peer->originate_timestamp = unix_to_ntp();
    peer->request_time = peer->originate_timestamp;

    pkt.transmit_timestamp = __builtin_bswap64(peer->originate_timestamp);

    return sendto(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr*) &peer->addr, peer->addr_len) == sizeof(pkt);
}

static void usage(void) {
    fprintf(stderr, "usage: ntpd [-c CONFIG]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    const char* config_path = _PATH_CONFIG;

    int c;
    while ((c = getopt(argc, argv, "c:")) != -1) {
        switch (c) {
            case 'c':
                config_path = optarg;
                break;
            default:
                usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc > 0) {
        warnx("extra operands provided");
        usage();
    }

    config_t* config = config_load_from_file(config_path);
    if (!config) {
        errx(EXIT_FAILURE, "failed to load config");
    }

    load_servers(config, &peer_list);

    config_free(config);

    if (!peer_list) {
        errx(EXIT_FAILURE, "failed to load NTP servers");
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        return EXIT_FAILURE;
    }

    for (;;) {
        for (struct ntp_peer* peer = peer_list; peer; peer = peer->next) {
            peer->usable = false;
            send_request(sockfd, peer);
        }

        struct pollfd pfd = {
            .fd = sockfd,
            .events = POLLIN,
        };

        struct timespec timeout = {
            .tv_sec = 5,
            .tv_nsec = 0,
        };

        while (ppoll(&pfd, 1, &timeout, NULL) > 0) {
            if (pfd.revents & POLLIN) {
                receive_response(sockfd);
            }

            pfd.revents = 0;

            timeout.tv_sec = 0;
            timeout.tv_nsec = 100000000;
        }

        struct ntp_peer* best = select_best_peer();
        if (best) {
            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
                err(EXIT_FAILURE, "clock_gettime(CLOCK_REALTIME)");
            }

            int64_t ns = ntp_offset_to_ns(best->offset);

            ts.tv_sec += ns / 1000000000LL;
            ts.tv_nsec += ns % 1000000000LL;

            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }

            if (ts.tv_nsec < 0) {
                ts.tv_sec--;
                ts.tv_nsec += 1000000000L;
            }

            if (clock_settime(CLOCK_REALTIME, &ts) < 0) {
                err(EXIT_FAILURE, "clock_settime(CLOCK_REALTIME)");
            }
        }

        sleep(NTP_INTERVAL);
    }

    close(sockfd);

    return EXIT_SUCCESS;
}
