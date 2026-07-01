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
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align) (!((x) & ((align) - 1)))

#define BITMAP_SET(bitmap, i) ((bitmap)[(i) / 64] |= (1ULL << ((i) & 63)))
#define BITMAP_CLEAR(bitmap, i) ((bitmap)[(i) / 64] &= ~(1ULL << ((i) & 63)))
#define BITMAP_TEST(bitmap, i) ((bitmap)[(i) / 64] & (1ULL << ((i) & 63)))

#define LOG2(x) (8 * 8 - __builtin_clzll((x)) - 1)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define SIZEOF_ARRAY(array) (sizeof((array)) / sizeof((array)[0]))

#define S_TO_NS(s)      ((uint64_t) (s) * 1000000000ul)
#define MS_TO_NS(ms)    ((uint64_t) (ms) * 1000000ul)
#define US_TO_NS(us)    ((uint64_t) (us) * 1000ul)

#define ALWAYS_INLINE __attribute__((always_inline)) inline

#endif /* _KERNEL_UTILS_MACROS_H */
