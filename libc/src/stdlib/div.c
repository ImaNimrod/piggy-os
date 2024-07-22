#include <stdlib.h>

div_t div(int x, int y) {
    return (div_t) { .quot = x / y, .rem = x % y };
}
