#include <stdio.h>
#include <stdlib.h>
#include <sys/sysact.h>

int main(void) {
    fputs("initiating full system shutdown...\n", stderr);
    sysact(SYSACT_MAGIC1, SYSACT_MAGIC2, SYSACT_MAGIC3, SYSACT_SHUTDOWN);
    return EXIT_SUCCESS;
}
