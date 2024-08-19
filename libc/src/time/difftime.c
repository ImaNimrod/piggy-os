#include "time_internal.h"

double difftime(time_t start, time_t end) {
    return (double) (end - start);
}
