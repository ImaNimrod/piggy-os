#include <cpu/asm.h>
#include <stddef.h>
#include <sys/timer.h>
#include <utils/cmdline.h>
#include <utils/log.h>
#include <utils/random.h>
#include <utils/spinlock.h>

// This is just an implementation of a 64-bit Mersenne Twister 19937 PRNG

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

static uint64_t mt_state[NN];
static size_t mt_index = NN + 1;
static int hardware_rng_source = HARDWARE_RNG_NONE;
static spinlock_t rng_lock;

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

    mt_state[0] = seed;
    for (mt_index = 1; mt_index < NN; mt_index++) {
        mt_state[mt_index] = (6364136223846793005ULL * (mt_state[mt_index - 1] ^ (mt_state[mt_index - 1] >> 62)) + mt_index);
    }
}

uint64_t rand64(void) {
    static uint64_t mag01[2] = { 0ULL, MATRIX_A };

    spinlock_acquire(&rng_lock);

    uint64_t x;

    if (mt_index >= NN) {
        if (mt_index == NN + 1) {
            seed_mt(time_realtime.tv_sec);
        }

        size_t i;
        for (i = 0; i < NN - MM; i++) {
            x = (mt_state[i] & UM) | (mt_state[i + 1] & LM);
            mt_state[i] = mt_state[i + MM] ^ (x >> 1) ^ mag01[x & 1ULL];
        }

        for (; i < NN - 1; i++) {
            x = (mt_state[i] & UM) | (mt_state[i + 1] & LM);
            mt_state[i] = mt_state[i + (MM - NN)] ^ (x >> 1) ^ mag01[x & 1ULL];
        }

        x = (mt_state[NN - 1] & UM) | (mt_state[0] & LM);
        mt_state[NN - 1] = mt_state[MM - 1] ^ (x >> 1) ^ mag01[x & 1ULL];
        mt_index = 0;
    }

    x = mt_state[mt_index++];

    x ^= (x >> 29) & 0x5555555555555555ULL;
    x ^= (x << 17) & 0x71d67fffeda60000ULL;
    x ^= (x << 37) & 0xfff7eee000000000ULL;
    x ^= (x >> 43);

    spinlock_release(&rng_lock);
    return x;
}

void random_init(void) {
    if (!cmdline_get("nocpurng")) {
        uint32_t ebx = 0, ecx = 0, unused;

        cpuid(7, 0, &unused, &ebx, &unused, &unused);
        if (ebx & (1 << 18)) {
            klog("[random] using rdseed to seed PRNG\n");
            hardware_rng_source = HARDWARE_RNG_RDSEED;
        } else {
            cpuid(1, 0, &unused, &unused, &ecx, &unused);
            if (ecx & (1 << 30)) {
                klog("[random] using rdrand to seed PRNG\n");
                hardware_rng_source = HARDWARE_RNG_RDRAND;
            } else {
                klog("[random] rdseed and rdrand both unavailable\n");
            }
        }
    } else {
        klog("[random] 'nocpurng' argument found, not using CPU random number generator instructions");
    }
}
