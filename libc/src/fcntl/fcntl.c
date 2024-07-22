#include <fcntl.h>
#include <stdarg.h> 

int fcntl(int fd, int cmd, ...) {
    (void) fd;
    (void) cmd;
#include <assert.h>
    assert("fcntl not implemented");
    return -1;
}
