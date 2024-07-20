#ifndef _KERNEL_UTILS_RANDOM_H
#define _KERNEL_UTILS_RANDOM_H

#include <stddef.h>
#include <stdint.h>
#include <utils/spinlock.h>

#define MT_SIZE         312
#define MT_PERIOD       156
#define MT_LOWER_MASK   ((uint64_t) (1 << 31) - 1)
#define MT_UPPER_MASK   ~(MT_LOWER_MASK)

struct rng_state {
    uint64_t state[MT_SIZE];
    size_t index;
    spinlock_t lock;
};

struct rng_state* rng_create(void);
uint64_t rng_rand(struct rng_state* rng);
size_t rng_rand_fill(struct rng_state* rng, void* buf, size_t count);
void rng_seed(struct rng_state* rng, uint64_t seed);

void random_init(void);

#endif /* _KERNEL_UTILS_RANDOM_H */
