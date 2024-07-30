#include "time_internal.h"

struct tm* gmtime(const time_t* time) {
    static struct tm buffer;
    return gmtime_r(time, &buffer);
}
