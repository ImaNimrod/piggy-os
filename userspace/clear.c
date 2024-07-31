#include <stdlib.h>
#include <unistd.h>

int main(void) {
    return write(STDOUT_FILENO, "\033[H\033[2J", 8) == 8 ? EXIT_SUCCESS : EXIT_FAILURE;
}
