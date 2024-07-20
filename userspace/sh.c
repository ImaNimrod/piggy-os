#include <stdio.h> 
#include <stdlib.h> 

int main(void) {
    fputc('a', stdout);
    fflush(stdout);
    return EXIT_SUCCESS;
}
