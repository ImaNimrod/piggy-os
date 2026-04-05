#include <stdlib.h>
#include <unistd.h>

static const char* CLEAR_CODE = "\033[H\033[2J";

int main(void) {
    return write(STDOUT_FILENO, CLEAR_CODE, sizeof(CLEAR_CODE)) == sizeof(CLEAR_CODE) ? EXIT_SUCCESS : EXIT_FAILURE;
}
