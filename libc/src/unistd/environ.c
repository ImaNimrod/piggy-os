#include <unistd.h>

char** environ;

char** __heap_environ;
size_t __heap_environ_length;
size_t __heap_environ_size;
