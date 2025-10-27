#ifndef _KERNEL_UTILS_MACROS_H
#define _KERNEL_UTILS_MACROS_H

#define likely(x) __builtin_expect(!!(x), 1) 
#define unlikely(x) __builtin_expect(!!(x), 0)

#define htons(x) __builtin_bswap16((x))
#define htonl(x) __builtin_bswap32((x))
#define ntohs(x) __builtin_bswap16((x))
#define ntohl(x) __builtin_bswap32((x))

#define makedev(maj, min) ((((maj) << 8) & 0xff00u) | ((min) & 0x00ffu))
#define major(dev) (((dev) & 0xff00u) >> 8)
#define minor(dev) ((dev) & 0x00ffu)

#define DIV_CEIL(x, div) (((x) + ((div) - 1)) / (div))

#define ALIGN_UP(x, align) (DIV_CEIL((x), (align)) * (align))
#define ALIGN_DOWN(x, align) (((x) / (align)) * (align))

#define BITMAP_SET(bitmap, i) ((bitmap)[(i) / 8] |=  (1 << ((i) % 8)))
#define BITMAP_CLEAR(bitmap, i) ((bitmap)[(i) / 8] &= ~(1 << ((i) % 8)))
#define BITMAP_TEST(bitmap, i) ((bitmap)[(i) / 8] & (1 << ((i) % 8)))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define SIZEOF_ARRAY(array) (sizeof((array)) / sizeof((array)[0]))

#define MS_TO_NS(ms) ((ms) * 1000000)
#define US_TO_NS(us) ((us) * 1000)

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define LIMINE_REQUEST __attribute__((used, section(".limine_requests")))
#define NORETURN __attribute__((noreturn))
#define USED __attribute__((used))

#define MAC_ADDRESS_FORMAT          "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDRESS_PRINT(mac)      (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]

#define IPV4_ADDRESS(a, b, c, d)    (((ipv4_address_t) a << 24) | ((ipv4_address_t) b << 16) | ((ipv4_address_t) c << 8) | ((ipv4_address_t) d))
#define IPV4_FORMAT                 "%u.%u.%u.%u"
#define IPV4_PRINT(ip)              ((ip) >> 24) & 0xff, ((ip) >> 16) & 0xff, ((ip) >> 8) & 0xff, (ip) & 0xff

#endif /* _KERNEL_UTILS_MACROS_H */
