#include <stdint.h>
#include <stdlib.h>

static uint64_t seed = 1;

int abs(int x) {
    return x >= 0 ? x : -x;
}

int rand(void) {
    seed = seed * 1103515245 + 12345;
    return (unsigned int) (seed / 65536) % 32768;
}

void srand(unsigned int s) {
    seed = s;
}
