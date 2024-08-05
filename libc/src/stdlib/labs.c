#include "stdlib_internal.h"

long int labs(long x) {
    return x >= 0 ? x : -x;
}
