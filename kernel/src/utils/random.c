#include <cpu/asm.h>
#include <stddef.h>
#include <sys/timer.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/random.h>

// This is just a simple implementation of a 64-bit Mersenne Twister 19937 PRNG

#define NN 312
#define MM 156
#define MATRIX_A 0xb5026f5aa96619e9ULL
#define UM 0xffffffff80000000ULL
#define LM 0x7fffffffULL

enum {
    HARDWARE_RNG_NONE,
    HARDWARE_RNG_RDRAND,
    HARDWARE_RNG_RDSEED,
};

static uint64_t mt[NN];
static size_t mti = NN + 1;
static int hardware_rng_source = HARDWARE_RNG_NONE;

static void seed_mt(uint64_t seed) {
    if (hardware_rng_source != HARDWARE_RNG_NONE) {
        uint64_t rand = 0;
        bool success = false;

        if (hardware_rng_source == HARDWARE_RNG_RDSEED) {
            success = rdseed(&rand);
        } else if (hardware_rng_source == HARDWARE_RNG_RDRAND) {
            success = rdrand(&rand);
        }

        if (success) {
            seed *= (seed ^ rand);
        }
    }

    mt[0] = seed;
    for (mti = 1; mti < NN; mti++) {
        mt[mti] = (6364136223846793005ULL * (mt[mti - 1] ^ (mt[mti - 1] >> 62)) + mti);
    }
}

uint64_t rand64(void) {
    static uint64_t mag01[2] = { 0ULL, MATRIX_A };

    uint64_t x;

    if (mti >= NN) {
        if (mti == NN + 1) {
            seed_mt(time_realtime.tv_sec);
        }

        size_t i;
        for (i = 0; i < NN - MM; i++) {
            x = (mt[i] & UM) | (mt[i + 1] & LM);
            mt[i] = mt[i + MM] ^ (x >> 1) ^ mag01[x & 1ULL];
        }

        for (; i < NN - 1; i++) {
            x = (mt[i] & UM) | (mt[i + 1] & LM);
            mt[i] = mt[i + (MM - NN)] ^ (x >> 1) ^ mag01[x & 1ULL];
        }

        x = (mt[NN - 1] & UM) | (mt[0] & LM);
        mt[NN - 1] = mt[MM - 1] ^ (x >> 1) ^ mag01[x & 1ULL];
        mti = 0;
    }

    x = mt[mti++];

    x ^= (x >> 29) & 0x5555555555555555ULL;
    x ^= (x << 17) & 0x71d67fffeda60000ULL;
    x ^= (x << 37) & 0xfff7eee000000000ULL;
    x ^= (x >> 43);

    return x;
}

void random_init(void) {
    if (!cmdline_get("nocpurng")) {
        uint32_t ebx = 0, ecx = 0, unused;
        if (cpuid(7, 0, &unused, &ebx, &unused, &unused) && ebx & (1 << 18)) {
            klog("[random] using rdseed to seed PRNG\n");
            hardware_rng_source = HARDWARE_RNG_RDSEED;
        } else if (cpuid(1, 0, &unused, &unused, &ecx, &unused) && ecx & (1 << 30)) {
            klog("[random] using rdrand to seed PRNG\n");
            hardware_rng_source = HARDWARE_RNG_RDRAND;
        } else {
            klog("[random] rdseed and rdrand both unavailable\n");
        }
    } else {
        klog("[random] 'nocpurng' argument found, not using CPU random number generator instructions");
    }

    seed_mt(time_realtime.tv_sec);
}
