#include <errno.h>
#include "time_internal.h"

time_t mktime(struct tm* tm) {
    int old_errno = errno;
    errno = 0;

    time_t result = timegm(tm);
    if (errno) {
        return -1;
    }

    errno = old_errno;
    return result;
}
