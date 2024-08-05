#include "stdlib_internal.h"

div_t div(int x, int y) {
    return (div_t) { .quot = x / y, .rem = x % y };
}
