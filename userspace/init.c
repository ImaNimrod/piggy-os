#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* message = "hello, world!";

int main(void) {
    write(STDOUT_FILENO, message, strlen(message));
    return EXIT_SUCCESS;
}
