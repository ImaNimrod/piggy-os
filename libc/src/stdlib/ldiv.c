#include <stdlib.h>

ldiv_t ldiv(long x, long y) {
    return (ldiv_t) { .quot = x / y, .rem = x % y };
}
