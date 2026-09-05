#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_arp.h>

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct iface_info {
    char name[IFNAMSIZ];

    unsigned int index;
    short flags;
    unsigned int mtu;

    unsigned char hwaddr[6];

    struct sockaddr_in addr;
    struct sockaddr_in netmask;
    struct sockaddr_in broadcast;

    bool have_addr;
    bool have_netmask;
    bool have_broadcast;
    bool have_hwaddr;
};

static bool colors = false;

static int compare_interfaces(const void* a, const void* b) {
    const struct iface_info* ia = a;
    const struct iface_info* ib = b;

    if (ia->index < ib->index) {
        return -1;
    }

    if (ia->index > ib->index) {
        return 1;
    }

    return strcmp(ia->name, ib->name);
}

static int get_ifreq(int fd, unsigned long request, const char* name, struct ifreq* ifr) {
    strncpy(ifr->ifr_name, name, IFNAMSIZ - 1);
    ifr->ifr_name[IFNAMSIZ - 1] = '\0';

    return ioctl(fd, request, ifr);
}

static struct iface_info* find_interface(struct iface_info* ifaces, size_t count, const char* name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(ifaces[i].name, name) == 0) {
            return &ifaces[i];
        }
    }

    return NULL;
}

static void print_flags(unsigned int flags) {
    bool first = true;

#define FLAG(name, value)                         \
    do {                                          \
        if (flags & (value)) {                    \
            if (!first) {                         \
                printf(",");                      \
            }                                     \
            printf(name);                         \
            first = false;                        \
        }                                         \
    } while (0)

    printf("<");

    FLAG("BROADCAST", IFF_BROADCAST);
    FLAG("LOOPBACK", IFF_LOOPBACK);
    FLAG("MULTICAST", IFF_MULTICAST);
    FLAG("UP", IFF_UP);
    FLAG("RUNNING", IFF_RUNNING);

    printf(">");
#undef FLAG
}

static void print_mac(const unsigned char* mac) {
    if (colors) {
        printf("\033[96m");
    }

    printf("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (colors) {
        printf("\033[0m");
    }
}

static void print_ip(const struct sockaddr_in *addr) {
    char buf[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) == NULL) {
        printf("?");
        return;
    }

    if (colors) {
        printf("\033[95m");
    }

    printf("%s", buf);

    if (colors) {
        printf("\033[0m");
    }
}

static int prefix_length(const struct sockaddr_in* netmask) {
    in_addr_t mask = ntohl(netmask->sin_addr.s_addr);
    int bits = 0;

    while (mask) {
        bits += mask & 1;
        mask >>= 1;
    }

    return bits;
}

static void usage(void) {
    fprintf(stderr, "usage: ifconf\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    while (getopt(argc, argv, "") != -1) {
        usage();
    }

    argc -= optind;
    argv += optind;

    if (argc >= 1) {
        warnx("extra operands provided");
        usage();
    }

    if (isatty(STDOUT_FILENO)) {
        colors = true;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        err(EXIT_FAILURE, "socket");
    }

    struct ifconf conf = {};
    if (ioctl(fd, SIOCGIFCONF, &conf) < 0) {
        err(EXIT_FAILURE, "ioctl(SIOCGIFCONF)");
    }

    if (conf.ifc_len == 0) {
        errx(EXIT_FAILURE, "no network interfaces found");
    }

    size_t buf_len = (size_t) conf.ifc_len;

    struct ifreq* reqs = malloc(buf_len);
    if (!reqs) {
        errx(EXIT_FAILURE, "malloc");
    }

    conf.ifc_len = buf_len;
    conf.ifc_req = reqs;

    if (ioctl(fd, SIOCGIFCONF, &conf) < 0) {
        err(EXIT_FAILURE, "ioctl(SIOCGIFCONF)");
    }

    size_t req_count = (size_t) conf.ifc_len / sizeof(struct ifreq);

    struct iface_info* ifaces = calloc(req_count, sizeof(struct iface_info));
    if (!ifaces) {
        err(EXIT_FAILURE, "calloc");
    }

    size_t iface_count = 0;

    for (size_t i = 0; i < req_count; i++) {
        struct ifreq* req = &reqs[i];

        if (find_interface(ifaces, iface_count, req->ifr_name)) {
            continue;
        }

        struct iface_info* iface = &ifaces[iface_count++];

        strncpy(iface->name, req->ifr_name, IFNAMSIZ - 1);
        iface->name[IFNAMSIZ - 1] = '\0';
    }

    free(reqs);

    for (size_t i = 0; i < iface_count; i++) {
        struct iface_info* iface = &ifaces[i];

        struct ifreq ifr;
        if (get_ifreq(fd, SIOCGIFINDEX, iface->name, &ifr) == 0) {
            iface->index = ifr.ifr_ifindex;
        }

        if (get_ifreq(fd, SIOCGIFFLAGS, iface->name, &ifr) == 0) {
            iface->flags = ifr.ifr_flags;
        }

        if (get_ifreq(fd, SIOCGIFMTU, iface->name, &ifr) == 0) {
            iface->mtu = ifr.ifr_mtu;
        }

        if (get_ifreq(fd, SIOCGIFADDR, iface->name, &ifr) == 0) {
            iface->addr = *(struct sockaddr_in*) &ifr.ifr_addr;
            iface->have_addr = true;
        }

        if (get_ifreq(fd, SIOCGIFNETMASK, iface->name, &ifr) == 0) {
            iface->netmask = *(struct sockaddr_in*) &ifr.ifr_netmask;
            iface->have_netmask = true;
        }

        if (get_ifreq(fd, SIOCGIFBRDADDR, iface->name, &ifr) == 0) {
            iface->broadcast = *(struct sockaddr_in*) &ifr.ifr_broadaddr;
            iface->have_broadcast = true;
        }

        if (get_ifreq(fd, SIOCGIFHWADDR, iface->name, &ifr) == 0) {
            memcpy(iface->hwaddr, ifr.ifr_hwaddr.sa_data, 6);
            iface->have_hwaddr = true;
        }
    }

    qsort(ifaces, iface_count, sizeof(ifaces[0]), compare_interfaces);

    for (size_t i = 0; i < iface_count; i++) {
        struct iface_info* iface = &ifaces[i];

        printf("%u: ", iface->index);

        if (colors) {
            printf("\033[97m");
        }

        printf("%s: ", iface->name);

        if (colors) {
            printf("\033[0m");
        }

        print_flags(iface->flags);

        printf(" mtu %u", iface->mtu);

        printf(" state ");

        bool running = iface->flags & IFF_RUNNING;

        if (colors) {
            printf("%s", running ? "\033[32m" : "\033[31m");
        }

        printf("%s", running ? "UP" : "DOWN");

        if (colors) {
            printf("\033[0m");
        }

        putchar('\n');

        printf("    link/");

        if (iface->flags & IFF_LOOPBACK) {
            printf("loopback ");
        } else {
            printf("ether ");
        }

        if (iface->have_hwaddr) {
            print_mac(iface->hwaddr);
        } else {
            printf("00:00:00:00:00:00");
        }

        putchar('\n');

        if (iface->have_addr) {
            printf("    inet ");

            print_ip(&iface->addr);

            if (iface->have_netmask) {
                printf("/%d", prefix_length(&iface->netmask));
            }

            if (iface->have_broadcast) {
                printf(" brd ");
                print_ip(&iface->broadcast);
            }

            putchar('\n');
        }

        if (i < iface_count - 1) {
            putchar('\n');
        }
    }

    free(ifaces);

    close(fd);
    return EXIT_SUCCESS;
}
