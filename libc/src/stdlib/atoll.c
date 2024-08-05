#include "stdlib_internal.h"

long long int atoll(const char* str) {
    return strtoll(str, NULL, 10);
}
