#include "time.h"

// TODO: implement timezones
struct tm* localtime(const time_t* time) {
    return gmtime(time);
}
