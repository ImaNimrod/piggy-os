#include <cpu/asm.h>
#include <cpuid.h>
#include <dev/hpet.h>
#include <utils/log.h>
#include <utils/random.h>
#include <utils/spinlock.h>

#define MT_SIZE     312
#define MT_PERIOD   156
#define MT_LOWER_MASK ((uint64_t) (1 << 31) - 1)
#define MT_UPPER_MASK ~(MT_LOWER_MASK)

static uint64_t mt[MT_SIZE];
static size_t index = MT_SIZE;

static spinlock_t rng_lock = {0};

static uint64_t generate(void) {
    if (index >= MT_SIZE) {
        for (size_t i = 0; i < MT_SIZE - 1; i++) {
            uint64_t x = (mt[i] & MT_UPPER_MASK) + (mt[(i + 1) % MT_SIZE] & MT_LOWER_MASK);
            uint64_t x_a = x >> 1;
            if ((x & 1) != 0) {
                x_a ^= 0xb5026f5aa96619e9;
            }

            mt[i] = mt[(i + MT_PERIOD) % MT_SIZE] ^ x_a;
        }

        index = 0;
    }

    uint64_t y = mt[index++];
    y ^= (y >> 29) & 0x5555555555555555;
    y ^= (y >> 17) & 0x71d67fffeda60000;
    y ^= (y >> 37) & 0xfff7eee000000000;
    y ^= (y >> 43);
    return y;
}

uint64_t rand(void) {
    spinlock_acquire(&rng_lock);
    uint64_t value = generate();
    spinlock_release(&rng_lock);
    return value;
}

size_t rand_fill(void* buf, size_t count) {
    spinlock_acquire(&rng_lock);

    uint8_t* buf_u8 = buf;
    size_t length = count;

    while (length >= 4) {
        uint64_t value = generate();
        buf_u8[0] = (uint8_t) (value & 0xff);
        buf_u8[1] = (uint8_t) ((value >> 8) & 0xff);
        buf_u8[2] = (uint8_t) ((value >> 16) & 0xff);
        buf_u8[3] = (uint8_t) ((value >> 24) & 0xff);
        buf_u8[4] = (uint8_t) ((value >> 32) & 0xff);
        buf_u8[5] = (uint8_t) ((value >> 40) & 0xff);
        buf_u8[6] = (uint8_t) ((value >> 48) & 0xff);
        buf_u8[7] = (uint8_t) ((value >> 56) & 0xff);

        buf_u8 += 8;
        length -= 8;
    }

    if (length > 0) {
        uint64_t value = generate();
        for (size_t i = 0; i < length; i++) {
            buf_u8[i] = (uint8_t) (value & 0xff);
            value >>= 8;
        }
    }

    spinlock_release(&rng_lock);
    return count;
}

void srand(uint64_t seed) {
    mt[0] = seed;
    for (size_t i = 1; i < MT_SIZE - 1; i++) {
        mt[i] = 0x5851f42d4c957f2d * (mt[i - 1] ^ (mt[i - 1] >> 62)) + i;
    }

    index = MT_SIZE;
}

void random_init(void) {
    uint64_t seed =  (0xed4fb45a8912123b * hpet_count()) ^ (0xa491beff476b1a90 * hpet_count());

    uint32_t ebx = 0, ecx = 0, unused;
    if (__get_cpuid(7, &unused, &ebx, &unused, &unused) && ebx & (1 << 18)) {
        klog("[random] seeding PRNG with rdseed...\n");
        seed *= seed ^ rdrand();
    } else if (__get_cpuid(1, &unused, &unused, &ecx, &unused) && ecx & (1 << 30)) {
        klog("[random] seeding PRNG with rdrand\n");
        seed *= seed ^ rdrand();
    } else {
        klog("[random] rdseed and rdrand unavailable\n");
    }

    srand(seed);
    klog("[random] initialized PRNG\n");
}
