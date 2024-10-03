#include <cpu/asm.h>
#include <dev/hpet.h>
#include <mem/slab.h>
#include <utils/log.h>
#include <utils/panic.h>
#include <utils/random.h>

struct rng_state* kgp_rng;

static bool use_rdseed = false;
static bool use_rdrand = false;

static uint64_t get_hardware_rand(void) {
    if (use_rdseed) {
        return rdseed();
    } else if (use_rdrand) {
        return rdrand();
    } 

    return (hpet_count() * 0xffab4587becdf092) ^ (hpet_count() * 0xbec79834da760912);
}

static uint64_t generate(struct rng_state* rng) {
    if (rng->index >= MT_SIZE) {
        for (size_t i = 0; i < MT_SIZE - 1; i++) {
            uint64_t x = (rng->state[i] & MT_UPPER_MASK) + (rng->state[(i + 1) % MT_SIZE] & MT_LOWER_MASK);
            uint64_t x_a = x >> 1;
            if (x & 1) {
                x_a ^= 0xb5026f5aa96619e9;
            }

            rng->state[i] = rng->state[(i + MT_PERIOD) % MT_SIZE] ^ x_a;
        }

        rng->index = 0;
    }

    uint64_t y = rng->state[rng->index++];
    y ^= (y >> 29) & 0x5555555555555555;
    y ^= (y >> 17) & 0x71d67fffeda60000;
    y ^= (y >> 37) & 0xfff7eee000000000;
    y ^= (y >> 43);
    return y;
}

struct rng_state* rng_create(void) {
    struct rng_state* rng = kmalloc(sizeof(struct rng_state));
    if (rng == NULL) {
        return NULL;
    }

    rng->index = MT_SIZE;
    rng->lock = (spinlock_t) {0};

    rng_seed(rng, hpet_count());
    return rng;
}

uint64_t rng_rand(struct rng_state* rng) {
    spinlock_acquire(&rng->lock);
    uint64_t value = generate(rng);
    spinlock_release(&rng->lock);
    return value;
}

size_t rng_rand_fill(struct rng_state* rng, void* buf, size_t count) {
    spinlock_acquire(&rng->lock);

    uint8_t* buf_u8 = buf;
    size_t length = count;

    while (length >= 8) {
        uint64_t value = generate(rng);
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
        uint64_t value = generate(rng);
        for (size_t i = 0; i < length; i++) {
            buf_u8[i] = (uint8_t) (value & 0xff);
            value >>= 8;
        }
    }

    spinlock_release(&rng->lock);
    return count;
}

void rng_seed(struct rng_state* rng, uint64_t seed) {
    spinlock_acquire(&rng->lock);

    rng->state[0] = seed *= seed ^ get_hardware_rand();
    for (size_t i = 1; i < MT_SIZE - 1; i++) {
        rng->state[i] = 0x5851f42d4c957f2d * (rng->state[i - 1] ^ (rng->state[i - 1] >> 62)) + i;
    }

    rng->index = MT_SIZE;
    spinlock_release(&rng->lock);
}

__attribute__((section(".unmap_after_init")))
void random_init(void) {
    uint32_t ebx = 0, ecx = 0, unused;
    if (cpuid(7, 0, &unused, &ebx, &unused, &unused) && ebx & (1 << 18)) {
        klog("[random] using rdseed to seed PRNGs\n");
        use_rdseed = true;
    } else if (cpuid(1, 0, &unused, &unused, &ecx, &unused) && ecx & (1 << 30)) {
        klog("[random] using rdrand to seed PRNGs\n");
        use_rdrand = true;
    } else {
        klog("[random] rdseed and rdrand both unavailable\n");
    }
    
    kgp_rng = rng_create();
    if (kgp_rng == NULL) {
        kpanic(NULL, false, "failed to create kernel general purpose RNG");
    }
}
