#include "stdio_internal.h"

void setbuf(FILE* stream, char*  buffer) {
    setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}
