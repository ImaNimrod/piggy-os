#include <stdio.h>
#include <stdlib.h>
#include <sys/sysact.h>

int main(void) {
    fputs("initiating system reboot...\n", stderr);
    sysact(SYSACT_MAGIC1, SYSACT_MAGIC2, SYSACT_MAGIC3, SYSACT_REBOOT);
    return EXIT_SUCCESS;
}
